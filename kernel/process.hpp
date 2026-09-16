#pragma once
#include "arch.hpp"
namespace process {
void initialize();
int64_t launch(const char* path, const char* arguments);
[[noreturn]] void start();
void timer(arch::InterruptFrame* frame);
void syscall(arch::InterruptFrame* frame);
void fault(arch::InterruptFrame* frame);
}
extern "C" void kernel_diagnostic(const char* command);
