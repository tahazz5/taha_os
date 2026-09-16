.DEFAULT_GOAL := all
.DELETE_ON_ERROR:

CXX := clang++
LD := $(shell command -v ld.lld 2>/dev/null || command -v ld)
HOSTCXX ?= g++
BUILD ?= build
export PATH := $(CURDIR)/build/tools/bin:$(PATH)
QEMU ?= qemu-system-x86_64
XORRISO ?= xorriso

CXXFLAGS := --target=x86_64-unknown-none-elf -std=c++20 \
	-ffreestanding -fno-exceptions -fno-rtti \
	-fno-stack-protector -fno-pic -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables \
	-mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
	-Wall -Wextra -Werror -g -O2 -MMD -MP -Ivendor/limine $(EXTRA_CXXFLAGS)
SOURCES := $(wildcard kernel/*.cpp)
OBJECTS := $(patsubst kernel/%.cpp,$(BUILD)/%.o,$(SOURCES)) $(BUILD)/interrupts.o $(BUILD)/embedded.o

.PHONY: all run debug test smoke check fault-smoke local-tools system-smoke
all: $(BUILD)/taha.iso $(BUILD)/data.img

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: kernel/%.cpp Makefile vendor/limine/limine.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -c $< -o $@

$(BUILD)/interrupts.o: kernel/interrupts.S Makefile | $(BUILD)
	$(CXX) --target=x86_64-unknown-none-elf -g -c $< -o $@

$(BUILD)/kernel.elf: $(OBJECTS) kernel/linker.ld
	$(LD) -static -z max-page-size=0x1000 -z noexecstack \
		-T kernel/linker.ld $(OBJECTS) -o $@

$(BUILD)/taha.iso: $(BUILD)/kernel.elf boot/limine.conf \
	vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin
	mkdir -p $(BUILD)/iso/boot/limine
	cp $(BUILD)/kernel.elf $(BUILD)/iso/boot/kernel.elf
	cp boot/limine.conf $(BUILD)/iso/boot/limine/limine.conf
	cp vendor/limine/limine-bios.sys vendor/limine/limine-bios-cd.bin $(BUILD)/iso/boot/limine/
	$(XORRISO) -as mkisofs -b boot/limine/limine-bios-cd.bin \
		-no-emul-boot -boot-load-size 4 -boot-info-table -o $@ $(BUILD)/iso

QEMUFLAGS := -accel tcg -m 128M -boot d -cdrom $(BUILD)/taha.iso \
	-drive file=$(BUILD)/data.img,format=raw,if=ide,index=0 \
	-display none -serial mon:stdio -nic none \
	-debugcon file:$(BUILD)/debug.log -global isa-debugcon.iobase=0xe9 \
	-no-shutdown

run: all
	$(QEMU) $(QEMUFLAGS)

debug: all
	$(QEMU) $(QEMUFLAGS) -no-reboot -S -gdb tcp:127.0.0.1:1234

$(BUILD)/pages-test: tests/pages.cpp kernel/pages.hpp Makefile | $(BUILD)
	$(HOSTCXX) -std=c++20 -Wall -Wextra -Werror -O2 $< -o $@


check: test $(BUILD)/kernel.elf
	python3 tests/elf.py $(BUILD)/kernel.elf

smoke: $(BUILD)/taha.iso
	python3 tests/smoke.py --qemu $(QEMU) --iso $(BUILD)/taha.iso

fault-smoke:
	$(MAKE) BUILD=build/fault EXTRA_CXXFLAGS=-DTAHA_TEST_FAULTS all
	python3 tests/smoke.py --qemu $(QEMU) --iso build/fault/taha.iso --faults

local-tools:
	python3 scripts/local-tools.py

-include $(OBJECTS:.o=.d)

PROGRAMS := shell hello counter fault check cat files notes guicheck
USER_ELFS := $(addprefix $(BUILD)/user/,$(addsuffix .elf,$(PROGRAMS)))
USERFLAGS := --target=x86_64-unknown-none-elf -std=c++20 -ffreestanding -fno-builtin \
	-fno-exceptions -fno-rtti -fno-stack-protector -fno-pic -fno-pie \
	-fno-unwind-tables -fno-asynchronous-unwind-tables -mno-red-zone \
	-mgeneral-regs-only -Wall -Wextra -Werror -O2 -MMD -MP

$(BUILD)/user:
	mkdir -p $@

$(BUILD)/user/%.o: user/%.cpp user/api.hpp shared/abi.hpp shared/gui.hpp user/gui.hpp Makefile | $(BUILD)/user
	$(CXX) $(USERFLAGS) -c $< -o $@

$(BUILD)/user/runtime.o: kernel/runtime.cpp Makefile | $(BUILD)/user
	$(CXX) $(USERFLAGS) -c $< -o $@

$(BUILD)/user/start.o: user/start.S | $(BUILD)/user
	$(CXX) --target=x86_64-unknown-none-elf -c $< -o $@

$(BUILD)/user/%.elf: $(BUILD)/user/%.o $(BUILD)/user/start.o $(BUILD)/user/runtime.o user/linker.ld
	$(LD) -static -z max-page-size=0x1000 -z noexecstack -T user/linker.ld $(filter %.o,$^) -o $@

$(BUILD)/embedded.S: $(USER_ELFS) scripts/embed.py
	python3 scripts/embed.py $@ $(USER_ELFS)

$(BUILD)/embedded.o: $(BUILD)/embedded.S
	$(CXX) --target=x86_64-unknown-none-elf -c $< -o $@

$(BUILD)/data.img:
	python3 scripts/disk.py $@

$(BUILD)/fs-test: tests/fs.cpp kernel/fs.cpp kernel/fs.hpp kernel/strings.hpp shared/abi.hpp | $(BUILD)
	$(HOSTCXX) -std=c++20 -Wall -Wextra -Werror -O2 tests/fs.cpp kernel/fs.cpp -o $@

test: $(BUILD)/editor-test $(BUILD)/pages-test $(BUILD)/fs-test $(BUILD)/loader-test $(BUILD)/user/hello.elf
	./$(BUILD)/editor-test
	./$(BUILD)/pages-test
	./$(BUILD)/fs-test
	./$(BUILD)/loader-test $(BUILD)/user/hello.elf

system-smoke: $(BUILD)/taha.iso
	python3 tests/system.py --qemu $(QEMU) --iso $(BUILD)/taha.iso

-include $(wildcard $(BUILD)/user/*.d)
.SECONDARY: $(addprefix $(BUILD)/user/,$(addsuffix .o,$(PROGRAMS)))

$(BUILD)/loader-test: tests/loader.cpp kernel/elf.cpp kernel/elf.hpp kernel/paging.hpp kernel/strings.hpp | $(BUILD)
	$(HOSTCXX) -std=c++20 -Wall -Wextra -Werror -O2 tests/loader.cpp kernel/elf.cpp -o $@

.PHONY: desktop display-smoke
DISPLAY_BACKEND ?= gtk
desktop: all
	$(QEMU) -accel tcg -m 128M -boot d -cdrom $(BUILD)/taha.iso \
		-drive file=$(BUILD)/data.img,format=raw,if=ide,index=0 \
		-display $(DISPLAY_BACKEND) -serial file:$(BUILD)/desktop-serial.log \
		-nic none -no-shutdown

display-smoke: $(BUILD)/taha.iso
	python3 tests/display.py --qemu $(QEMU) --iso $(BUILD)/taha.iso

$(BUILD)/editor-test: tests/editor.cpp shared/text_editor.hpp | $(BUILD)
	$(HOSTCXX) -std=c++20 -Wall -Wextra -Werror -O2 $< -o $@
