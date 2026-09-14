#!/usr/bin/env python3
"""Actual BIOS boots with bounded serial exchanges; transcripts survive failures."""
import argparse
import os
from pathlib import Path
import re
import selectors
import subprocess
import time

class Guest:
    def __init__(self, args, memory, name, disk=None, reboot=False):
        self.log = Path(args.iso).parent / f'{name}-{memory}.log'
        self.output = bytearray()
        self.cursor = 0
        self.p = subprocess.Popen([
            args.qemu, '-accel', 'tcg', '-m', str(memory), '-boot', 'd',
            '-cdrom', args.iso, '-display', 'none', '-monitor', 'none',
            '-serial', 'stdio', '-nic', 'none', '-no-shutdown',
            *([] if reboot else ['-no-reboot']),
            *(['-drive', f'file={disk},format=raw,if=ide,index=0'] if disk else []),
        ], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        self.selector = selectors.DefaultSelector()
        self.selector.register(self.p.stdout, selectors.EVENT_READ)

    def expect(self, text):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            found = self.output.find(text, self.cursor)
            if found >= 0:
                result = bytes(self.output[self.cursor:found + len(text)])
                self.cursor = found + len(text)
                return result
            if self.selector.select(0.1):
                chunk = os.read(self.p.stdout.fileno(), 4096)
                if not chunk:
                    break
                self.output.extend(chunk)
        raise AssertionError(f'Missing {text!r}; see {self.log}\n{self.output.decode(errors="replace")}')

    def send(self, data):
        # Stay within the emulated UART FIFO even on slower TCG hosts.
        for offset in range(0, len(data), 8):
            self.p.stdin.write(data[offset:offset + 8])
            self.p.stdin.flush()
            time.sleep(0.012)

    def command(self, text, response, ending=b'\r'):
        self.send(text + ending)
        result = self.expect(b'taha> ')
        assert response in result, result
        return result

    def boot(self):
        result = self.expect(b'taha> ')
        for marker in (b'GDT/TSS/IDT ready', b'Owned page tables active', b'TIMER PASS', b'SELFTEST PASS', b'Userspace ready'):
            assert marker in result, result

    def close(self):
        self.p.kill()
        remaining, _ = self.p.communicate(timeout=5)
        self.output.extend(remaining)
        self.selector.close()
        self.log.write_bytes(self.output)


def normal(args, memory):
    guest = Guest(args, memory, 'smoke')
    try:
        guest.boot()
        guest.command(b'help', b'memmap  selftest')
        guest.command(b'mem', b'page size: 4096 bytes')
        guest.command(b'vm', b'stack guards: 4 | null page: unmapped')
        before = guest.command(b'uptime', b'Timer ticks:')
        # More commands give the periodic interrupt time to run.
        for _ in range(3):
            guest.command(b'selftest', b'SELFTEST PASS')
        after = guest.command(b'uptime', b'Timer ticks:')
        tick = lambda text: int(re.search(rb'Timer ticks: (\d+)', text)[1])
        assert tick(after) > tick(before), 'Timer stopped delivering interrupts'
        guest.command(b'infx\x7fo', b'TahaOS 0.4', ending=b'\r\n')
        guest.command(b'cpu', b'CPU:', ending=b'\n')
        guest.command(b'unknown', b'Unknown command')
        guest.command(b'x' * 140, b'Command too long')
        guest.command(b'info', b'TahaOS 0.4') # recover after overflow
        guest.command(b'fault ud', b'Unknown command') # production excludes fault hooks
        guest.send(b'halt\r')
        guest.expect(b'System halted.')
        assert b'PANIC' not in guest.output and b'SELFTEST FAIL' not in guest.output
        print(f'PASS: BIOS boot, serial monitor, memory, timer, interrupt return ({memory} MiB)', flush=True)
    finally:
        guest.close()


def fault(args, command, vector, detail, error):
    guest = Guest(args, 128, 'fault-' + command.decode().replace(' ', '-'))
    try:
        guest.boot()
        guest.send(command + b'\r')
        guest.expect(b'PANIC: exception vector=' + str(vector).encode())
        report = guest.expect(b'\n')
        assert detail in report and b' error=' + error + b' ' in report, report
        print(f'PASS: {command.decode()} -> exception {vector} {detail.decode()}', flush=True)
    finally:
        guest.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--iso', default='build/taha.iso')
    parser.add_argument('--faults', action='store_true')
    args = parser.parse_args()
    if args.faults:
        fault(args, b'fault ud', 6, b'invalid opcode', b'0x0')
        fault(args, b'fault page', 14, b'page fault', b'0x2')
        fault(args, b'fault text', 14, b'page fault', b'0x3')
        fault(args, b'fault stack', 8, b'double fault, IST1', b'0x0')
    else:
        for memory in (32, 128, 512):
            normal(args, memory)

if __name__ == '__main__':
    main()
