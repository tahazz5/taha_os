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
IRQ1 reçoit le clavier PS/2 traduit en scancodes set 1, IRQ4 reçoit COM1. Les
IRQ7/15 parasites sont traités par vérification des registres in-service.
Clavier et UART alimentent une file de 1024 octets, avec notification de perte.
La disposition clavier est US ; les touches de navigation et la souris ne sont
pas implémentées.

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

Le framebuffer RGB 32 bits affiche un terminal à police bitmap, une barre de
titre et des repères de commandes. La sortie réelle de la console s'y reflète,
avec défilement, curseur et effacement ANSI simple. Aucun serveur de fenêtres,
protocole graphique utilisateur ou contrôle cliquable n'est simulé. Les glyphes
ASCII dérivent de DejaVu Sans Mono ; le fichier généré est versionné pour que
la compilation ne dépende pas de FreeType/Pillow.

## Validation et limites des preuves

Les tests hôte couvrent l'allocateur, le chargeur ELF hostile et TahaFS avec
échec d'écriture et corruption d'une banque. Les tests ELF contrôlent l'entrée,
les segments et l'absence de symboles/runtime non résolus. Les tests QEMU
couvrent les démarrages, l'entrée série et PS/2, le framebuffer, la préemption
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
