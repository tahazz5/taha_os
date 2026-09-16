# Architecture de TahaOS 0.4

## Démarrage

Limine entre dans `kentry` en mode 64 bits. Le stub désactive les interruptions,
efface DF, installe une pile noyau de 64 Kio et appelle C++ avec l'alignement ABI
System V. COM1 initialise la sortie ; le framebuffer RGB 32 bits, s'il est
compatible, ajoute un terminal local. La console série reste disponible en
l'absence de framebuffer.

Le noyau installe GDT, TSS et IDT, copie les tables de pages du bootloader dans
ses propres frames, puis retire les pages de garde et la page virtuelle zéro.
CR3 bascule sur la copie ; CR4.PGE est temporairement effacé pour vider les
traductions globales, CR0.WP est activé. Le CPU doit prendre en charge NX ;
EFER.NXE est activé. CR0.TS interdit l'emploi de FPU/SIMD sans gestion d'état.

Après les tests mémoire/interruption, le noyau installe les fichiers système,
monte TahaFS sur le maître IDE primaire s'il est reconnu, charge le shell ELF,
initialise le clavier PS/2, le PIC et le PIT. Un IRQ timer doit arriver avant
que le démarrage soit déclaré réussi. `IRETQ` lance ensuite le shell en ring 3.

## Mémoire

Deux bitmaps de 128 Kio couvrent les frames sous 4 Gio. L'une indique les pages
utilisables, l'autre les pages libres. Page zéro et toutes les régions autres
que `LIMINE_MEMMAP_USABLE` restent réservées. Les plages sont alignées vers
l'intérieur ; les recouvrements sont rejetés sans modification de l'allocateur.
La première allocation interdit tout ajout ultérieur de régions. La libération
rejette les adresses non alignées, réservées, hors plage ou déjà libres.

Le clonage des tables noyau suit les entrées non-feuilles présentes, avec une
limite de 4096 frames de tables. Les mappings 1 Gio/2 Mio sont conservés, sauf
lorsqu'une garde nécessite de les subdiviser. Un échec libère les nouvelles
tables avant toute modification de CR3. Les réponses Limine, l'ancienne pile
et les anciennes tables ne sont pas récupérées.

Chaque processus reçoit une nouvelle PML4 : moitié basse vide, moitié haute
partagée avec le noyau et privée du bit utilisateur. Ses pages propres sont
référencées dans une liste bornée de 256 frames, tables incluses. Elles sont
effacées avant utilisation, puis toutes libérées à la récupération du processus.

Les ELF sont chargés entre `0x400000` et `0x700000`. La pile utilisateur occupe
`0x7f0000`–`0x800000`, avec une garde non mappée dessous. Le chargeur valide les
en-têtes, bornes, segments, alignements et point d'entrée ; il refuse le code
modifiable, les segments se recouvrant à l'échelle des pages et les images
nécessitant un chargeur dynamique. BSS et pile démarrent à zéro. Les données et
la pile sont NX. Un échec de chargement détruit l'espace partiellement construit.

Les mappings HHDM sont réservés au superviseur. Ils restent des alias modifiables
de certaines pages physiques : les protections ne forment pas une frontière
contre du code noyau arbitraire. L'allocateur et les tables ne sont pas conçus
pour SMP ; les handlers matériels n'allouent pas de mémoire.

## Interruptions et privilèges

La GDT contient code/data noyau (sélecteurs 8/16), TSS 64 bits (24), puis
code/data utilisateur (43/51 avec RPL 3). Le TSS utilise une pile de syscall
commune à la CPU. #DF, NMI et #MC disposent de piles IST distinctes de 16 Kio,
chacune précédée d'une garde de 4 Kio. La pile noyau principale est également
guardée. Les handlers ne garantissent pas la survie à des fautes récursives
sur les piles d'urgence.

Les stubs de 256 vecteurs normalisent le couple vecteur/code d'erreur. Les
vecteurs 8, 10–14, 17, 21, 29 et 30 portent un code poussé par le CPU ; les autres
reçoivent zéro. L'entrée commune sauvegarde les 15 GPR, efface DF, aligne RSP et
appelle C++. Le retour restaure GPR et frame CPU par `IRETQ`. Le test noyau
charge des valeurs distinctes dans les GPR, positionne CF/DF et vérifie leur
conservation après `INT3`.

Le PIC est déplacé vers les vecteurs 32–47. IRQ0 pilote le PIT à environ 100 Hz,
IRQ1 reçoit le clavier PS/2 traduit en scancodes set 1, IRQ4 reçoit COM1 et
IRQ12 reçoit la souris PS/2 (paquets de trois octets). Le PIC autorise aussi
IRQ2 pour la cascade du contrôleur esclave. Les
IRQ7/15 parasites sont traités par vérification des registres in-service.
L'UART alimente une file console de 1024 octets, avec notification de perte.
Le clavier graphique et la souris passent par une file de 256 événements, traitée
par l'ordonnanceur avant le réveil des tâches. Le clavier rejoint la console
uniquement lorsque Terminal a le focus ; Notes reçoit son propre texte. Sans
framebuffer compatible, le clavier rejoint directement la console. La disposition
clavier est US. Les scancodes étendus transmettent les flèches, Home/End,
Page Up/Down et Delete à Notes ; les raccourcis Ctrl, Alt+Tab et F1–F3 passent
par cette même file. Les touches de navigation ne sont pas transmises au shell.

Les erreurs utilisateur terminent le processus avec le statut `128 + vecteur`.
Les erreurs superviseur impriment les registres utiles et arrêtent la machine.
Une faute du shell initial reste fatale : il n'existe pas encore de superviseur
capable de le relancer. Le test #DF réduit RSP à la limite de la garde principale
et provoque une écriture ; le CPU doit utiliser IST1 pour signaler la double faute.

## Ordonnanceur

La table contient 16 slots et des PID monotones. Les états sont :

| Valeur | État |
| --- | --- |
| 0 | Libre |
| 1 | Exécutable |
| 2 | Attend une entrée console |
| 3 | Attend un enfant |
| 4 | Attend une échéance timer |
| 5 | Zombie, statut conservé pour le parent |
| 6 | Attend un événement de fenêtre |

Le timer préempte uniquement du code utilisateur. À une interruption ou à la
sortie d'un appel système, l'ordonnanceur sauvegarde la frame CPU du processus,
passe sur CR3 noyau, réveille les tâches éligibles et choisit le prochain slot
exécutable en round-robin. La frame d'interruption est remplacée et CR3 charge
l'espace choisi avant `IRETQ`.

Le noyau n'est pas préemptible durant un appel système. Sa pile commune peut
donc être réutilisée : aucun processus n'a de continuation noyau suspendue.
Quand toutes les tâches attendent, le noyau utilise `STI; HLT; CLI`. Les handlers
timer/entrée peuvent s'exécuter sur cette pile, mais ne changent pas de processus
si la frame interrompue venait du ring 0.

`wait` récupère le statut et les pages de l'enfant. Les tâches détachées et les
enfants orphelins sont récupérés automatiquement une fois terminés. Le PID 1
est réservé au shell et ne peut être tué par l'appel `kill`. Ctrl+C termine les enfants directs du shell au premier plan avec le statut 130 ;
il ne constitue pas une implémentation générale des signaux ou groupes de processus.
Il n'y a ni UID,
permissions par utilisateur, priorité, temps réel, thread ni compatibilité POSIX.
Les longues opérations PIO et framebuffer retardent les IRQ ; le nombre de ticks
n'est donc pas une horloge murale précise.

## Appels système

`INT 0x80` utilise une porte DPL 3. RAX porte le numéro, RDI/RSI/RDX les arguments ;
RAX renvoie le résultat signé. Les autres registres sont préservés. Les numéros
et structures stables pour cette version figurent dans `shared/abi.hpp`.

| Famille | Opérations |
| --- | --- |
| Console | Écriture bornée, lecture bloquante d'un caractère |
| Processus | spawn, wait, exit, yield, sleep, ticks, liste et kill |
| Fichiers | Lecture/écriture complète, list, stat, mkdir, unlink, sync |
| Système | Diagnostics et reboot, réservés au shell initial |
| Graphique | window_open, window_present, window_event, window_close, desktop_launch |

Chaque pointeur est validé par parcours des tables utilisateur et des permissions
avant copie. Les transferts passent par des buffers noyau, jamais par une
déréférence directe d'un pointeur utilisateur. Les longueurs sont bornées avant
arithmétique et copie. Les fichiers sont limités à 32 Kio et les chemins à 95
caractères plus zéro terminal. Les lectures fichier reprennent à l'offset zéro ;
il n'existe pas encore de descripteurs ouverts ni de `seek`.

La chaîne d'argument, de 127 caractères maximum, est copiée au sommet de la pile
et passée dans RDI à `_start`. Le runtime appelle `main(const char*)`, puis
`exit` avec sa valeur de retour réduite à un octet (0–255). Le code utilisateur est compilé sans SSE/FPU,
libc, allocation dynamique ni déroulement d'exceptions.

## TahaFS et PIO

Le pilote ATA utilise le maître IDE primaire, LBA28 et les commandes PIO
IDENTIFY/READ/WRITE/FLUSH, avec des boucles d'attente bornées. Les interruptions
ATA sont désactivées. Le disque doit porter le label `TAHADK01` au secteur zéro ;
les autres formats sont laissés intacts.

Les 64 nœuds (répertoires inclus) résident en RAM. Les fichiers intégrés de
`/bin` et `/etc` sont en lecture seule ; `/tmp` ne persiste pas. Les descendants
de `/home` sont sérialisés sous forme d'entrées fixes de 104 octets suivies des
données. Les chemins, tailles, doublons et parents sont validés intégralement
avant de publier un instantané lu sur disque.

Les deux banques commencent aux secteurs 1 et 8193. Chaque banque dispose de
8192 secteurs, avec un en-tête de 512 octets contenant signature, génération,
taille et somme FNV-1a du contenu. Une transaction :

1. Invalide l'en-tête de la banque inactive et exécute FLUSH.
2. Écrit le contenu puis exécute FLUSH.
3. Publie le nouvel en-tête puis exécute FLUSH.

Le montage choisit la génération valide la plus récente, ou revient à l'autre
banque en cas de contenu corrompu. Les mutations échouées restaurent le nœud en
RAM. Comme avec un disque réel, une erreur lors du FLUSH final laisse une
incertitude sur la durabilité de la dernière transaction. La somme de contrôle
ne fournit pas d'authenticité cryptographique. Le format n'est pas compatible
avec les systèmes de fichiers Linux.

## Affichage

Le framebuffer RGB 32 bits (640×480 à 1920×1080) affiche un bureau avec trois
fenêtres : Terminal, Files et Notes. Une pile définit leur ordre visuel ; le clic
change le focus, la barre de titre permet le déplacement, la réduction et la
maximisation/restauration. La barre inférieure et F1–F3 ouvrent les applications ;
Alt+Tab parcourt les fenêtres visibles. Fermer Terminal masque sa fenêtre sans terminer le shell. Fermer Files termine
son processus ; Notes propose de sauvegarder ses modifications avant de quitter. Au lancement du shell, la grille locale
affiche les raccourcis du bureau ; les diagnostics de démarrage restent sur la
console série.

Files parcourt TahaFS avec pagination, création de répertoire et aperçu limité
à 2047 octets. L'action Edit in Notes charge le fichier dans l'éditeur. Le modèle
`shared/text_editor.hpp` contient un document borné à 32768 octets, une position
de curseur et un indicateur de modification ; il fournit insertion, suppression,
navigation et conversion entre position et ligne/colonne visuelle. Les tabulations
s'affichent comme une cellule. Un historique borné de 256 insertions/suppressions
permet Undo/Redo et restaure le curseur. Chaque modification reçoit une révision
distincte ; la révision sauvegardée détermine le marqueur de modification, même
après undo/redo ou création d’une nouvelle branche. Un chargement réussi remet
l’historique à zéro ; un chargement refusé le conserve. Notes utilise les transactions TahaFS pour Save et
Save As. Open valide tout le texte avant de remplacer le document courant ; les
fichiers binaires et trop grands sont refusés. Les permissions TahaFS interdisent
l'écriture des fichiers système, qui peuvent être copiés sous un autre nom.

Une machine d'états de dialogue gère les chemins, les erreurs, le remplacement
d'un autre fichier et les modifications non enregistrées lors d'un changement
de document. Une opération en attente n'est exécutée qu'après sauvegarde réussie
ou abandon explicite. Échap annule. Ces dialogues ne bloquent pas les processus
utilisateur : ils ne capturent que les entrées locales du bureau. Le redémarrage
reste une commande du shell et ne demande pas d'enregistrer les documents ouverts.

Files et Notes sont des ELF en ring 3 avec leurs propres espaces d’adressage.
Le noyau conserve le compositeur et les décorations. `shared/gui.hpp` définit
les commandes de dessin et les événements ; `user/gui.hpp` fournit les widgets.

`window_open` réserve le slot 1 ou 2 au PID appelant ; le slot 0 appartient au
terminal. Un processus possède au plus une fenêtre et ne peut prendre celle
d’un autre. `window_present` copie et valide au plus 512 commandes (rectangles
ou texte ASCII terminé par zéro), puis remplace la liste affichée. Le dessin
est limité à la zone cliente, sans accès utilisateur au framebuffer.

`window_event` délivre redimensionnement, touche, clic, fermeture, ouverture de
document ou perte d’entrées. Son second argument vaut 0 pour une lecture immédiate
(`abi::again` si vide), ou 1 pour bloquer sans consommer de CPU. La file circulaire
contient au plus 63 événements ; une saturation déclenche `input_lost` et permet
aux applications d’annuler leurs dialogues incomplets. Les notifications de
redimensionnement et de fermeture sont conservées séparément.

`desktop_launch` démarre Files/Notes en tâche détachée ou transmet un chemin à
l’instance existante. `window_close`, la sortie, une faute et `kill` libèrent la
fenêtre et ses événements. Fermer une fenêtre sans en posséder est sans effet.
Les pointeurs et permissions sont vérifiés comme pour les autres appels système.
Le terminal conserve sa grille, son défilement et son effacement ANSI simple.
Deux buffers statiques de 1920×1080 pixels occupent environ 16 Mio : composition
en RAM, puis copie des seuls pixels modifiés vers le framebuffer. Le rafraîchissement
est limité à une fois tous les trois ticks. Les IRQ restent autorisées pendant
la peinture ; elles ne font qu'enfiler les entrées et le timer ne préempte pas
le code noyau. Le traitement des événements et les opérations TahaFS ont lieu
avec les IRQ désactivées, hors des handlers, sans réentrance du système de fichiers.
Une saturation de la file abandonne les nouveaux événements et annule le glissement
en cours pour éviter de conserver un bouton bloqué. Les paniques forcent l'affichage
du terminal avant l'arrêt. Les glyphes ASCII dérivent de DejaVu Sans Mono.

## Validation et limites des preuves

Les tests hôte couvrent le modèle d'édition (insertion, suppression, navigation,
retour visuel à la ligne, saturation et refus de texte invalide), l'allocateur, le chargeur ELF hostile et TahaFS avec
échec d'écriture et corruption d'une banque. Les tests ELF contrôlent l'entrée,
les segments et l'absence de symboles/runtime non résolus. Les tests QEMU
couvrent les démarrages, l'entrée série et PS/2, le framebuffer, les clics,
le déplacement et la fermeture/réouverture des fenêtres, la sauvegarde et le
rechargement de Notes à froid, son aperçu dans Files, la création de dossiers et
de documents nommés, l'édition au curseur par clic et clavier, les dialogues
d'annulation/erreur et la maximisation/restauration, la préemption
de deux boucles CPU, la récupération des pages après sortie/kill, les fautes
utilisateur, les protections de pages et les redémarrages avec données.

La borne 4 Gio est testée sur l'hôte ; les machines virtuelles utilisent jusqu'à
512 Mio. NMI/#MC, panne physique pendant FLUSH, matériel réel, UEFI et SMP ne sont
pas validés. Le logiciel reste un OS éducatif pour le PC émulé QEMU.

## Références

Les structures TSS/IST, frames, privilèges et protections suivent le
[manuel système Intel](https://www.intel.com/content/www/us/en/developer/articles/technical/intel-sdm.html).
Les invocations suivent la [documentation QEMU](https://qemu-project.gitlab.io/qemu/system/invocation.html).
Le contrat du bootloader est défini par le header Limine 8.7 vendored.
