# TahaOS 0.4

TahaOS est un petit système d'exploitation autonome en C++20 pour x86-64.
Il démarre son propre noyau, affiche un terminal, exécute un shell en mode
utilisateur, ordonnance plusieurs programmes et conserve les fichiers sur un
disque virtuel. Il fonctionne dans QEMU, sans Linux à l'intérieur de la machine
virtuelle.

Il s'agit d'un OS éducatif utilisable, avec des limites explicites : ce n'est pas
un système de bureau généraliste comparable à Linux ou Windows.

## Lancer le système

Dans cet espace de travail, les outils QEMU et xorriso sont disponibles dans
`build/tools`. Depuis un environnement disposant d'un affichage graphique :

```sh
make desktop
```

Le terminal s'affiche dans une fenêtre QEMU, avec un clavier **QWERTY US**.
La barre latérale est un aide-mémoire ; elle ne contient pas de boutons cliquables.
Le système n'a pas de souris ni de gestionnaire de fenêtres.

Sans affichage graphique, utiliser la console série :

```sh
make run
```

Quitter QEMU : Ctrl+A, puis X en mode série, ou fermer sa fenêtre.
`halt` synchronise les fichiers puis arrête le système invité. `reboot` le
redémarre. Ne pas lancer deux QEMU simultanément avec le même disque de données.

Pour installer les dépendances sur Ubuntu/Debian :

```sh
sudo apt-get install clang lld make git g++ binutils python3 xorriso qemu-system-x86 qemu-system-gui
# Seulement si vendor/limine est absent :
git clone --depth=1 --branch=v8.7.0-binary https://github.com/limine-bootloader/limine.git vendor/limine
make
```

Sans sudo, avec Clang, GNU ld, make, Python et apt déjà présents :

```sh
make local-tools
make
```

Ce script extrait les outils et leurs dépendances manquantes dans `build/tools`.
Il cible Debian/Ubuntu x86-64 avec des index apt disponibles ; ce n'est pas une
chaîne hermétique. `make desktop` nécessite toujours une session X11/Wayland.
`DISPLAY_BACKEND=sdl`, `QEMU=...` et `XORRISO=...` permettent de remplacer les
valeurs par défaut. LLD est préféré, GNU ld est également pris en charge.

## Essayer les fonctions

À l'invite `taha>` :

```text
help
ls /
cat /etc/welcome
mkdir documents
write documents/note.txt "Bonjour TahaOS"
cat documents/note.txt
edit documents/note.txt
run hello "mon premier programme"
bg counter
ps
sync
reboot
```

Dans l'éditeur, une ligne contenant seulement `.` enregistre le texte ;
`.abort` annule. L'éditeur remplace le contenu du fichier. `ps` donne les PID ;
`kill <PID>` permet d'arrêter un programme lancé en arrière-plan.

| Commandes | Fonction |
| --- | --- |
| `pwd`, `cd`, `ls` | Parcourir les répertoires |
| `cat`, `write`, `append`, `edit` | Lire ou modifier un fichier texte |
| `mkdir`, `cp`, `rm` | Créer, copier, supprimer ; répertoires vides seulement |
| `echo` | Afficher du texte |
| `apps`, `run`, `bg` | Lister et exécuter les programmes |
| `ps`, `kill`, `sleep` | Observer/arrêter les processus ; attente en secondes |
| `info`, `cpu`, `mem`, `memmap`, `vm`, `uptime` | Diagnostics système |
| `selftest` | Vérifier mémoire, gardes et retour d'interruption |
| `clear`, `help`, `sync`, `halt`, `reboot` | Terminal et contrôle du système |

Les chemins relatifs partent de `/home`. `.` et `..`, les guillemets et les
échappements simples sont pris en charge. Les lignes sont limitées à 127
caractères et 12 arguments ; pas de pipes, de redirections ni de scripts shell.
Les noms de fichier ne contiennent pas d'espaces et le terminal utilise l'ASCII.

## Programmes utilisateur

Les programmes sont de vrais ELF statiques, compilés séparément du noyau.
Ils s'exécutent au niveau de privilège 3, dans des espaces d'adressage distincts.

| Programme | Exemple |
| --- | --- |
| `hello` | `run hello "Bonjour"` affiche notamment son niveau de privilège |
| `cat` | `run cat /etc/welcome` lit un fichier via les appels système |
| `check` | `run check` teste les pointeurs invalides et les permissions |
| `counter` | `bg counter` boucle sans céder le CPU ; le timer le préempte |
| `fault` | `run fault kernel` provoque une faute limitée au processus |
| `shell` | Shell initial, PID 1 ; seul processus autorisé à lire le clavier |

`run` attend la fin du programme et affiche son code de sortie. Ctrl+C interrompt
le programme au premier plan (code 130), annule une attente ou vide la ligne en
cours. `bg` détache le programme ; utiliser `kill` pour l’arrêter.
`run fault`, `run fault io`, `run fault nx` et `run fault text` vérifient aussi
les instructions invalides, les accès aux ports et les protections mémoire.
Le shell reste disponible après la terminaison de ces processus.

On peut copier un ELF dans `/home` et le lancer après redémarrage :

```text
cp /bin/hello.elf /home/bonjour.elf
run /home/bonjour.elf
```

L'ABI minimale est documentée dans [docs/architecture.md](docs/architecture.md)
et définie dans `shared/abi.hpp`. `user/api.hpp` fournit les fonctions d'appel.
Pour ajouter un programme embarqué, créer `user/nom.cpp`, ajouter son nom à
`PROGRAMS` dans le Makefile et l'enregistrer dans `kernel/programs.cpp`.

## Disque et fichiers

`make` crée **une seule fois** `build/data.img`, un disque de données de 16 Mio.
Une compilation ultérieure ne l'efface pas. L'image ISO `build/taha.iso` contient
le noyau et les programmes système ; les données personnelles sont dans le
fichier de disque séparé.

- `/home` : fichiers persistants, synchronisés lors de chaque modification.
- `/tmp` : fichiers temporaires, perdus au redémarrage.
- `/bin` et `/etc` : fichiers système intégrés, en lecture seule.

TahaFS utilise deux instantanés alternés, une somme de contrôle et un en-tête
publié après les données. Si le dernier instantané est invalide, le montage
essaie le précédent. Le pilote ne monte que les images portant la signature
`TAHADK01` créée par notre script ; il ne formate pas un disque inconnu.
Sans disque reconnu, le système annonce **RAM only** et les fichiers ne sont
pas persistants. Ce format n'est ni FAT ni ext4 et n'est pas montable par Linux.

## Vérification

```sh
make check          # Allocateur, TahaFS, chargeur ELF, contrat binaire
make smoke          # Démarrages BIOS avec 32, 128 et 512 Mio
make system-smoke   # Processus, fautes isolées, fichiers, redémarrages
make display-smoke  # Vrai clavier PS/2, framebuffer et capture PNG
make fault-smoke    # Fautes fatales du noyau dans une ISO de diagnostic séparée
```

Ces tests utilisent QEMU TCG et des disques temporaires distincts de vos données.
Les journaux sont dans `build/*.log` et `build/fault/*.log` ; la capture d'écran
est `build/desktop.png`. Le workflow GitHub Actions exécute ces mêmes suites et
conserve les journaux. Il est configuré mais n'a pas été exécuté sur GitHub depuis
cet espace de travail.

## Limites actuelles

- Une CPU ; 16 processus simultanés ; pas d'utilisateurs multiples.
- 64 entrées de fichiers/répertoires, système inclus ; 32 Kio par fichier.
- Environ 1 Mio de pages physiques au maximum par processus, tables comprises.
- RAM utilisable gérée sous 4 Gio ; allocations statiques/bornées, sans heap général.
- ELF statiques TahaOS uniquement, sans libc/POSIX ni compatibilité des exécutables Linux.
- Appels système non préemptibles ; pas de garanties temps réel.
- Pas de réseau, audio, USB, FPU/SIMD utilisateur, navigateur ou installation sur PC réel.
- BIOS et périphériques PC de QEMU testés ; UEFI, matériel physique et SMP non validés.

Le noyau, le système de fichiers, l'ABI et les protections sont détaillés dans
[docs/architecture.md](docs/architecture.md). Pour GDB : `make debug`, puis
`target remote 127.0.0.1:1234` et `hbreak *kentry` dans `gdb build/kernel.elf`.

Limine : `v8.7.0-binary`, commit `aad3edd370955449717a334f0289dee10e2c5f01`.
La police bitmap dérive de DejaVu Sans Mono ; licence dans
[assets/FONT-LICENSE.txt](assets/FONT-LICENSE.txt).
