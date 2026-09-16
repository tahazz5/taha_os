#include "programs.hpp"
#include "fs.hpp"
#include "console.hpp"
#define PROGRAM(name) extern "C" const unsigned char app_##name##_start[], app_##name##_end[];
PROGRAM(shell)
PROGRAM(hello)
PROGRAM(counter)
PROGRAM(fault)
PROGRAM(check)
PROGRAM(cat)
PROGRAM(files)
PROGRAM(notes)
PROGRAM(guicheck)
void install_programs() {
#define INSTALL(name) if (!fs::seed("/bin/" #name ".elf", app_##name##_start, \
    reinterpret_cast<uintptr_t>(app_##name##_end) - reinterpret_cast<uintptr_t>(app_##name##_start))) \
    console::panic("cannot install " #name);
    INSTALL(shell)
    INSTALL(hello)
    INSTALL(counter)
    INSTALL(fault)
    INSTALL(check)
    INSTALL(cat)
    INSTALL(files)
    INSTALL(notes)
    INSTALL(guicheck)
    constexpr char welcome[] = "Bienvenue dans TahaOS.\n/home conserve vos fichiers sur le disque virtuel.\nTapez help pour les commandes et apps pour les programmes.\n";
    if (!fs::seed("/etc/welcome", welcome, sizeof(welcome) - 1)) console::panic("cannot install welcome file");
}
