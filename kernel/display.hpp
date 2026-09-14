#pragma once
struct limine_framebuffer;
namespace display {
bool initialize(limine_framebuffer* framebuffer);
void putc(char c);
}
