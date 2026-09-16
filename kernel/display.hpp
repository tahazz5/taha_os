#pragma once
#include "../shared/gui.hpp"
struct limine_framebuffer;
namespace display {
bool initialize(limine_framebuffer* framebuffer);
void putc(char c);
void ready();
// IRQ handlers queue input; the scheduler services it with interrupts disabled.
void mouse(int dx, int dy, unsigned buttons);
using namespace gui;
bool key(int c);
void service();
void flush();
int64_t open(uint64_t pid, const gui::Config& config);
int64_t present(uint64_t pid, const gui::Command* commands, size_t count);
int64_t event(uint64_t pid, gui::Event& event);
void release(uint64_t pid);
int64_t launch(unsigned slot, const char* path="");
}
