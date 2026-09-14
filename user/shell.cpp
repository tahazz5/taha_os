#include "api.hpp"
using namespace os;
namespace {
char cwd[abi::path_max] = "/home";
char content[abi::file_max];
char line[128];
abi::Entry entries[64];
bool carriage = false;

bool readline(char* buffer, size_t capacity) {
    size_t size = 0; bool overflow = false;
    for (;;) {
        int64_t input = call(abi::console_read);
        if (input < 0) {
            if (input == -2) { print("\nInput overrun; line discarded.\n"); size = 0; overflow = true; continue; }
            print("Console unavailable.\n"); return false;
        }
        char c = input;
        if (c == 3) { print("^C\n"); buffer[0] = 0; carriage = false; return true; }
        if (c == '\n' && carriage) { carriage = false; continue; }
        carriage = c == '\r';
        if (c == '\r' || c == '\n') {
            print("\n"); buffer[size] = 0;
            if (overflow) { print("Command too long.\n"); buffer[0] = 0; }
            return true;
        }
        if (c == 8 || c == 127) {
            if (size && !overflow) { --size; print("\b \b"); }
        } else if (c >= 32 && c <= 126) {
            if (size + 1 < capacity && !overflow) { buffer[size++] = c; write(&c, 1); }
            else overflow = true;
        }
    }
}
bool resolve(const char* text, char* path) {
    char combined[2 * abi::path_max]; size_t n = 0;
    if (*text != '/') {
        for (size_t i = 0; cwd[i]; ++i) combined[n++] = cwd[i];
        if (n > 1) combined[n++] = '/';
    }
    for (size_t i = 0; text[i]; ++i) { if (n + 1 == sizeof(combined)) return false; combined[n++] = text[i]; }
    combined[n] = 0; path[0] = '/'; size_t out = 1;
    for (size_t i = 0; i < n;) {
        while (combined[i] == '/') ++i;
        size_t start = i; while (combined[i] && combined[i] != '/') ++i;
        size_t len = i - start; if (!len) break;
        if (len == 1 && combined[start] == '.') continue;
        if (len == 2 && combined[start] == '.' && combined[start + 1] == '.') {
            while (out > 1 && path[out - 1] != '/') --out;
            if (out > 1) --out;
            continue;
        }
        if (out + (out > 1 ? 1 : 0) + len >= abi::path_max) return false;
        if (out > 1) path[out++] = '/';
        for (size_t j = 0; j < len; ++j) path[out++] = combined[start + j];
    }
    path[out] = 0; return true;
}
int parse(char* input, char** args) {
    unsigned read = 0, write = 0; int count = 0;
    while (input[read]) {
        while (input[read] == ' ') ++read;
        if (!input[read]) break;
        if (count == 12) return -1;
        args[count++] = input + write; char quote = 0;
        while (input[read]) {
            char c = input[read++];
            if (!quote && c == ' ') break;
            if ((c == '\'' || c == '"') && (!quote || quote == c)) { quote = quote ? 0 : c; continue; }
            if (c == '\\' && input[read]) c = input[read++];
            input[write++] = c;
        }
        if (quote) return -1;
        input[write++] = 0;
    }
    return count;
}
size_t join(char** args, int start, int count, char* out, size_t capacity) {
    size_t length = 0;
    for (int i = start; i < count; ++i) {
        if (i > start && length + 1 < capacity) out[length++] = ' ';
        for (size_t j = 0; args[i][j] && length + 1 < capacity; ++j) out[length++] = args[i][j];
    }
    out[length] = 0; return length;
}
bool integer(const char* text, uint64_t& value) {
    value = 0; if (!*text) return false;
    while (*text) {
        if (*text < '0' || *text > '9' || value > (UINT64_MAX - unsigned(*text - '0')) / 10) return false;
        value = value * 10 + *text++ - '0';
    }
    return true;
}
void listing(const char* path) {
    int64_t n = call(abi::list, uint64_t(path), uint64_t(entries), 64);
    if (n < 0) { error(n); return; }
    for (int64_t i = 0; i < n; ++i) {
        print(entries[i].directory ? "d " : "f "); print(entries[i].path);
        print("  "); number(entries[i].size); if (entries[i].readonly) print(" [ro]"); print("\n");
    }
}
void execute(char** args, int count) {
    if (!count) return;
    const char* cmd = args[0]; char path[abi::path_max];
    if (equal(cmd, "help")) {
        print("TahaOS shell (ring 3)\n"
              "help  info  cpu  mem  memmap  selftest  uptime  vm  clear  halt\n"
              "pwd  cd  ls  cat  write  append  edit  cp  rm  mkdir  echo\n"
              "apps  run  bg  ps  kill  sleep  sync  reboot\n"
              "Examples: write note.txt \"Bonjour\" | run hello | bg counter\n");
    } else if (equal(cmd, "pwd")) { print(cwd); print("\n"); }
    else if (equal(cmd, "echo")) { join(args, 1, count, content, sizeof(content)); print(content); print("\n"); }
    else if (equal(cmd, "apps")) listing("/bin");
    else if (equal(cmd, "ls")) {
        if (resolve(count > 1 ? args[1] : ".", path)) listing(path); else error(abi::invalid);
    } else if (equal(cmd, "cd") && count == 2) {
        abi::Entry entry;
        if (!resolve(args[1], path)) { error(abi::invalid); return; }
        int64_t result = call(abi::stat, uint64_t(path), uint64_t(&entry));
        if (result < 0) error(result);
        else if (!entry.directory) print("Not a directory.\n");
        else copy(cwd, path, sizeof(cwd));
    } else if ((equal(cmd, "cat") || equal(cmd, "rm") || equal(cmd, "mkdir")) && count == 2) {
        if (!resolve(args[1], path)) { error(abi::invalid); return; }
        if (equal(cmd, "cat")) {
            int64_t n = readfile(path, content, sizeof(content));
            if (n < 0) error(n); else { write(content, n); if (!n || content[n - 1] != '\n') print("\n"); }
        } else error(call(equal(cmd, "rm") ? abi::unlink : abi::mkdir, uint64_t(path)));
    } else if ((equal(cmd, "write") || equal(cmd, "append")) && count >= 2) {
        if (!resolve(args[1], path)) { error(abi::invalid); return; }
        size_t prefix = 0;
        if (equal(cmd, "append")) {
            int64_t n = readfile(path, content, sizeof(content));
            if (n < 0 && n != abi::missing) { error(n); return; }
            if (n > 0) prefix = n;
        }
        char text[128]; size_t size = join(args, 2, count, text, sizeof(text));
        if (prefix + size + 1 > sizeof(content)) { error(abi::full); return; }
        for (size_t i = 0; i < size; ++i) content[prefix + i] = text[i];
        content[prefix + size] = '\n'; error(writefile(path, content, prefix + size + 1));
    } else if (equal(cmd, "cp") && count == 3) {
        char target[abi::path_max];
        if (!resolve(args[1], path) || !resolve(args[2], target)) { error(abi::invalid); return; }
        int64_t n = readfile(path, content, sizeof(content));
        if (n < 0) error(n); else error(writefile(target, content, n));
    } else if (equal(cmd, "edit") && count == 2) {
        if (!resolve(args[1], path)) { error(abi::invalid); return; }
        print("Enter replacement text. A single '.' saves; '.abort' cancels.\n");
        size_t size = 0;
        for (;;) {
            print("edit> "); if (!readline(line, sizeof(line))) return;
            if (equal(line, ".abort")) { print("Cancelled.\n"); return; }
            if (equal(line, ".")) break;
            size_t n = length(line); if (size + n + 1 > sizeof(content)) { error(abi::full); return; }
            for (size_t i = 0; i < n; ++i) content[size++] = line[i]; content[size++] = '\n';
        }
        int64_t result = writefile(path, content, size); if (result < 0) error(result); else print("Saved.\n");
    } else if ((equal(cmd, "run") || equal(cmd, "bg")) && count >= 2) {
        bool slash = false; for (size_t i = 0; args[1][i]; ++i) if (args[1][i] == '/') slash = true;
        if (slash) { if (!resolve(args[1], path)) { error(abi::invalid); return; } }
        else {
            if (length(args[1]) + 10 >= sizeof(path)) { error(abi::invalid); return; }
            copy(path, "/bin/", sizeof(path)); copy(path + 5, args[1], sizeof(path) - 5);
            copy(path + length(path), ".elf", 5);
        }
        char argument[128]; join(args, 2, count, argument, sizeof(argument));
        bool background = equal(cmd, "bg");
        int64_t pid = call(abi::spawn, uint64_t(path), uint64_t(argument), background);
        if (pid < 0) { error(pid); return; }
        if (background) {
            char message[64]; copy(message, "Started PID ", sizeof(message));
            size_t n = 12 + decimal(message + 12, pid); message[n++] = '\n'; write(message, n);
        }
        else {
            int64_t status = call(abi::wait, pid);
            if (status < 0) error(status);
            else { print("Exit status: "); number(status); print("\n"); }
        }
    } else if (equal(cmd, "ps")) {
        abi::Process processes[16]; int64_t n = call(abi::processes, uint64_t(processes), 16);
        print("PID PPID TICKS STATE PROGRAM\n");
        for (int64_t i = 0; i < n; ++i) {
            number(processes[i].pid); print(" "); number(processes[i].parent); print(" ");
            number(processes[i].ticks); print(" "); number(processes[i].state); print(" "); print(processes[i].name); print("\n");
        }
    } else if ((equal(cmd, "kill") || equal(cmd, "sleep")) && count == 2) {
        uint64_t value;
        if (!integer(args[1], value) || (equal(cmd, "sleep") && value > 86400)) { error(abi::invalid); return; }
        error(call(equal(cmd, "kill") ? abi::kill : abi::sleep, equal(cmd, "kill") ? value : value * 100));
    } else if (equal(cmd, "sync")) {
        int64_t result = call(abi::sync); if (result < 0) error(result); else print("Disk synchronized.\n");
    } else if (equal(cmd, "reboot")) error(call(abi::reboot));
    else {
        char diagnostic[128]; join(args, 0, count, diagnostic, sizeof(diagnostic));
        error(call(abi::diagnostic, uint64_t(diagnostic)));
    }
}
}
extern "C" int main(const char*) {
    print("Userspace ready | ELF programs | preemptive scheduling\n");
    for (;;) {
        print("taha> "); if (!readline(line, sizeof(line))) return 1;
        char* args[12]; int n = parse(line, args);
        if (n < 0) print("Invalid quoting or too many arguments.\n"); else execute(args, n);
    }
}
