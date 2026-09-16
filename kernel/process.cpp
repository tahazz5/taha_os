#include "process.hpp"
#include "display.hpp"
#include "paging.hpp"
#include "elf.hpp"
#include "fs.hpp"
#include "console.hpp"
#include "strings.hpp"
#include "../shared/abi.hpp"
namespace {
enum State { free_slot, runnable, reading, waiting, sleeping, zombie, window_wait };
struct Task {
    paging::AddressSpace space;
    arch::InterruptFrame context;
    uint64_t pid, parent, cpu_ticks, wake, child, event_address;
    int64_t status;
    State state;
    char name[32];
};
constinit Task tasks[16]{};
int current = -1;
uint64_t next_pid = 1;
uint8_t io_buffer[abi::file_max];
gui::Command draw_buffer[gui::command_limit];
constinit paging::AddressSpace kernel_space{};
extern "C" [[noreturn]] void resume_user(arch::InterruptFrame* frame);
Task* find(uint64_t pid) {
    for (auto& t : tasks) if (t.state != free_slot && t.pid == pid) return &t;
    return nullptr;
}
void reap(Task& t) { paging::destroy(t.space); t.state = free_slot; }
int64_t spawn(const char* path, const char* args, uint64_t parent) {
    Task* task = nullptr;
    for (auto& t : tasks) if (t.state == free_slot) { task = &t; break; }
    if (!task || next_pid == UINT64_MAX) return abi::full;
    size_t size; const uint8_t* data = fs::file(path, size);
    if (!data) return abi::missing;
    uint64_t entry;
    if (!elf::load(task->space, data, size, entry)) return abi::invalid;
    if (!paging::copy_to(task->space, 0x7fff80, args, strings::length(args) + 1)) {
        paging::destroy(task->space); return abi::invalid;
    }
    task->context = {};
    task->context.rip = entry; task->context.cs = 0x2b; task->context.ss = 0x33;
    task->context.flags = 0x202; task->context.rsp = 0x7fff00; task->context.rdi = 0x7fff80;
    task->pid = next_pid++; task->parent = parent; task->cpu_ticks = 0;
    task->wake = task->child = 0; task->status = 0; task->state = runnable;
    strings::copy(task->name, path, sizeof(task->name));
    return task->pid;
}
void terminate(Task& task, int64_t status) {
    display::release(task.pid);
    task.status = status; task.state = zombie;
    for (auto& t : tasks) if (t.state != free_slot && t.parent == task.pid) t.parent = 0;
}
void wake_tasks() {
    if (console::take_interrupt()) {
        bool child_interrupted = false;
        for (auto& t : tasks) if (t.state != free_slot && t.state != zombie && t.parent == 1) {
            terminate(t, 130); child_interrupted = true;
        }
        auto* shell = find(1);
        if (child_interrupted) console::write("^C\n");
        else if (shell && shell->state == sleeping) {
            shell->context.rax = 0; shell->state = runnable; console::write("^C\n");
        } else console::deliver_interrupt();
    }
    for (auto& t : tasks) {
        if (t.state == reading) {
            int c = console::read_char();
            if (c != -1) { t.context.rax = int64_t(c); t.state = runnable; }
        } else if (t.state == window_wait) {
            gui::Event event{};
            int64_t result=display::event(t.pid,event);
            if (result!=abi::again) {
                if (result==0 && !paging::copy_to(t.space,t.event_address,&event,sizeof(event))) result=abi::invalid;
                t.context.rax=result; t.state=runnable;
            }
        } else if (t.state == sleeping && arch::ticks() >= t.wake) {
            t.context.rax = 0; t.state = runnable;
        } else if (t.state == waiting) {
            auto* child = find(t.child);
            if (!child || child->state == zombie) {
                t.context.rax = child ? child->status : abi::missing;
                if (child) reap(*child);
                t.state = runnable;
            }
        }
    }
    for (auto& t : tasks) if (t.state == zombie && t.parent == 0) reap(t);
}
void schedule(arch::InterruptFrame* frame) {
    if (current >= 0 && tasks[current].state != free_slot) tasks[current].context = *frame;
    paging::activate(kernel_space);
    for (;;) {
        display::service();
        wake_tasks();
        for (int offset = 1; offset <= 16; ++offset) {
            int index = (current + offset) % 16;
            if (tasks[index].state == runnable) {
                current = index; *frame = tasks[index].context;
                paging::activate(tasks[index].space); return;
            }
        }
        // All tasks blocked: only hardware IRQs run until a task can wake.
        asm volatile("sti; hlt; cli" : : : "memory");
    }
}
bool text(const Task& t, uint64_t source, char* target, size_t capacity) {
    for (size_t i = 0; i < capacity; ++i) {
        if (!paging::copy_from(t.space, target + i, source + i, 1)) return false;
        if (!target[i]) return true;
    }
    return false;
}
}
namespace process {
int64_t launch(const char* path, const char* arguments) { return spawn(path,arguments,0); }
void initialize() {
    size_t unused = 0;
    if (!fs::file("/bin/shell.elf", unused)) console::panic("shell executable missing");
    if (spawn("/bin/shell.elf", "", 0) != 1) console::panic("cannot load userspace shell");
}
[[noreturn]] void start() {
    asm volatile("cli" : : : "memory");
    display::ready();
    current = 0;
    paging::activate(tasks[0].space);
    resume_user(&tasks[0].context);
}
void timer(arch::InterruptFrame* frame) {
    if ((frame->cs & 3) != 3 || current < 0) return;
    ++tasks[current].cpu_ticks;
    schedule(frame);
}
void fault(arch::InterruptFrame* frame) {
    if (current < 0) console::panic("user exception without process");
    auto& task = tasks[current];
    console::write("\nProcess "); console::number(task.pid); console::write(" fault vector=");
    console::number(frame->vector); console::write(" error="); console::hex(frame->error);
    console::write(" rip="); console::hex(frame->rip); console::write(" (terminated)\n");
    if (task.pid == 1) console::panic("userspace shell faulted");
    terminate(task, 128 + frame->vector); schedule(frame);
}
void syscall(arch::InterruptFrame* f) {
    if (current < 0 || (f->cs & 3) != 3) console::panic("invalid syscall origin");
    auto& task = tasks[current];
    char path[abi::path_max], argument[128];
    int64_t result = abi::invalid;
    switch (f->rax) {
    case abi::console_write:
        if (f->rsi <= 4096 && paging::copy_from(task.space, io_buffer, f->rdi, f->rsi)) {
            for (size_t i = 0; i < f->rsi; ++i) console::putc(io_buffer[i]);
            result = f->rsi;
        }
        break;
    case abi::console_read:
        if (task.pid != 1) { result = abi::denied; break; }
        result = console::read_char();
        if (result == -1) task.state = reading;
        break;
    case abi::exit:
        if (task.pid == 1) { result = abi::denied; break; }
        terminate(task, int64_t(f->rdi & 255)); result = 0; break;
    case abi::spawn:
        if (text(task, f->rdi, path, sizeof(path)) && text(task, f->rsi, argument, sizeof(argument)))
            result = spawn(path, argument, f->rdx ? 0 : task.pid);
        break;
    case abi::wait: {
        auto* child = find(f->rdi);
        if (!child || child->parent != task.pid) result = abi::missing;
        else { task.child = child->pid; task.state = waiting; result = 0; }
        break;
    }
    case abi::sleep:
        if (f->rdi <= 8640000) { task.wake = arch::ticks() + f->rdi; task.state = sleeping; result = 0; }
        break;
    case abi::yield: result = 0; break;
    case abi::ticks: result = arch::ticks(); break;
    case abi::file_read:
        if (f->rdx <= abi::file_max && text(task, f->rdi, path, sizeof(path)) &&
            paging::user_range(task.space, f->rsi, f->rdx, true)) {
            result = fs::read(path, io_buffer, f->rdx);
            if (result >= 0) paging::copy_to(task.space, f->rsi, io_buffer, result);
        }
        break;
    case abi::file_write:
        if (f->rdx <= abi::file_max && text(task, f->rdi, path, sizeof(path)) &&
            paging::copy_from(task.space, io_buffer, f->rsi, f->rdx)) result = fs::write(path, io_buffer, f->rdx);
        break;
    case abi::mkdir: if (text(task, f->rdi, path, sizeof(path))) result = fs::mkdir(path); break;
    case abi::unlink: if (text(task, f->rdi, path, sizeof(path))) result = fs::remove(path); break;
    case abi::stat: {
        abi::Entry entry;
        if (text(task, f->rdi, path, sizeof(path)) && paging::user_range(task.space, f->rsi, sizeof(entry), true)) {
            result = fs::stat(path, entry);
            if (!result) paging::copy_to(task.space, f->rsi, &entry, sizeof(entry));
        }
        break;
    }
    case abi::list: {
        abi::Entry entries[fs::max_nodes];
        if (f->rdx <= fs::max_nodes && text(task, f->rdi, path, sizeof(path)) &&
            paging::user_range(task.space, f->rsi, f->rdx * sizeof(abi::Entry), true)) {
            result = fs::list(path, entries, f->rdx);
            if (result >= 0) paging::copy_to(task.space, f->rsi, entries, result * sizeof(abi::Entry));
        }
        break;
    }
    case abi::processes: {
        abi::Process entries[16]{}; size_t n = 0;
        if (f->rsi > 16 || !paging::user_range(task.space, f->rdi, f->rsi * sizeof(abi::Process), true)) break;
        for (auto& t : tasks) if (t.state != free_slot && n < f->rsi) {
            entries[n].pid = t.pid; entries[n].parent = t.parent; entries[n].ticks = t.cpu_ticks;
            entries[n].state = t.state; strings::copy(entries[n].name, t.name, sizeof(entries[n].name)); ++n;
        }
        paging::copy_to(task.space, f->rdi, entries, n * sizeof(abi::Process)); result = n; break;
    }
    case abi::kill: {
        auto* victim = find(f->rdi);
        if (!victim) result = abi::missing;
        else if (victim->pid == 1) result = abi::denied;
        else { terminate(*victim, 137); result = 0; }
        break;
    }
    case abi::diagnostic:
        if (task.pid != 1) { result = abi::denied; break; }
        if (text(task, f->rdi, argument, sizeof(argument))) {
            paging::activate(kernel_space); kernel_diagnostic(argument); paging::activate(task.space); result = 0;
        }
        break;
    case abi::sync: result = fs::sync() ? 0 : abi::io; break;
    case abi::window_open: {
        gui::Config config{};
        if (paging::copy_from(task.space,&config,f->rdi,sizeof(config))) result=display::open(task.pid,config);
        break;
    }
    case abi::window_present:
        if (f->rsi<=gui::command_limit && paging::copy_from(task.space,draw_buffer,f->rdi,f->rsi*sizeof(gui::Command)))
            result=display::present(task.pid,draw_buffer,f->rsi);
        break;
    case abi::window_event: {
        gui::Event event{};
        if (f->rsi>1 || !paging::user_range(task.space,f->rdi,sizeof(event),true)) break;
        result=display::event(task.pid,event);
        if (result==0) paging::copy_to(task.space,f->rdi,&event,sizeof(event));
        else if (result==abi::again && f->rsi==1) { task.event_address=f->rdi; task.state=window_wait; }
        break;
    }
    case abi::window_close: display::release(task.pid); result=0; break;
    case abi::desktop_launch:
        if (f->rdi>=1 && f->rdi<=2 && text(task,f->rsi,path,sizeof(path))) result=display::launch(f->rdi,path);
        break;
    case abi::reboot:
        if (task.pid != 1) { result = abi::denied; break; }
        if (fs::persistent() && !fs::sync()) { result = abi::io; break; }
        for (unsigned i = 0; i < 100000; ++i) if (!(arch::in(0x64) & 2)) { arch::out(0x64, 0xfe); break; }
        arch::halt();
    default: break;
    }
    f->rax = result;
    // Syscalls run with IF clear; scheduling occurs only at this return boundary.
    schedule(f);
}
}
