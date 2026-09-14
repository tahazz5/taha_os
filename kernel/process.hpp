#pragma once
#include "arch.hpp"
namespace process {
void initialize();
[[noreturn]] void start();
void timer(arch::InterruptFrame* frame);
void syscall(arch::InterruptFrame* frame);
void fault(arch::InterruptFrame* frame);
}
extern "C" void kernel_diagnostic(const char* command);
