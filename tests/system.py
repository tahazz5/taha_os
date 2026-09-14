#!/usr/bin/env python3
"""End-to-end user processes, storage transactions and reboot persistence."""
import argparse
from pathlib import Path
import re
import tempfile
from smoke import Guest


def memory(guest):
    output = guest.command(b'mem', b'free:')
    return int(re.search(rb'free: (\d+)', output)[1])


def session(args, disk):
    guest = Guest(args, 128, 'system-write', disk)
    try:
        guest.boot()
        assert b'Storage: TahaFS persistent /home' in guest.output
        guest.command(b'pwd', b'/home')
        guest.command(b'cat /etc/welcome', b'Bienvenue')
        guest.command(b'mkdir docs', b'taha>')
        guest.command(b'write docs/note.txt "Bonjour TahaOS"', b'taha>')
        guest.command(b'append docs/note.txt "Deuxieme ligne"', b'taha>')
        guest.command(b'cat docs/note.txt', b'Bonjour TahaOS\r\nDeuxieme ligne')
        guest.command(b'cp docs/note.txt docs/copy.txt', b'taha>')
        guest.command(b'cd docs', b'taha>')
        guest.command(b'pwd', b'/home/docs')
        guest.command(b'cd .././', b'taha>')
        guest.command(b'pwd', b'/home')
        guest.command(b'rm docs', b'Permission denied')
        guest.command(b'write /bin/hello.elf bad', b'Permission denied')
        guest.command(b'write /tmp/temporary hello', b'taha>')
        guest.send(b'edit docs/edited.txt\r')
        guest.expect(b'edit> ')
        guest.send(b'Une premiere ligne\r')
        guest.expect(b'edit> ')
        guest.send(b'Une seconde ligne\r')
        guest.expect(b'edit> ')
        guest.send(b'.\r')
        assert b'Saved.' in guest.expect(b'taha> ')
        baseline = memory(guest)
        guest.command(b'run hello "depuis le shell"', b'Hello from an ELF process in ring 3!')
        guest.command(b'run check', b'USERCHECK PASS')
        guest.command(b'run cat /home/docs/note.txt', b'Bonjour TahaOS')
        guest.command(b'cp /bin/hello.elf docs/program.elf', b'taha>')
        guest.command(b'run docs/program.elf', b'Hello from an ELF process in ring 3!')
        guest.command(b'write docs/invalid.elf bad', b'taha>')
        guest.command(b'run docs/invalid.elf', b'Invalid argument or executable')
        for fault, vector in ((b'', 6), (b'kernel', 14), (b'io', 13), (b'nx', 14), (b'text', 14)):
            output = guest.command(b'run fault ' + fault, b'(terminated)')
            assert b'fault vector=' + str(vector).encode() in output, output
            assert b'Exit status: ' + str(128 + vector).encode() in output, output
        for _ in range(3):
            guest.command(b'run hello', b'Exit status: 0')
        assert memory(guest) == baseline, 'Process address spaces leaked frames'
        # Both workers spin forever without yielding or making syscalls.
        first = guest.command(b'bg counter first', b'Started PID')
        second = guest.command(b'bg counter second', b'Started PID')
        pids = [int(re.search(rb'Started PID (\d+)', text)[1]) for text in (first, second)]
        guest.command(b'sleep 1', b'taha>')
        ps = guest.command(b'ps', b'PID PPID TICKS STATE PROGRAM')
        for pid in pids:
            match = re.search(rb'\r?\n' + str(pid).encode() + rb' \d+ (\d+) \d+ /bin/counter.elf', ps)
            assert match and int(match[1]) > 0, ps
            guest.command(b'kill ' + str(pid).encode(), b'taha>')
        guest.command(b'kill 1', b'Permission denied')
        assert memory(guest) == baseline, 'Killed processes leaked frames'
        guest.send(b'run counter foreground\r')
        guest.expect(b'CPU worker started foreground')
        guest.send(b'\x03')
        assert b'Exit status: 130' in guest.expect(b'taha> ')
        assert memory(guest) == baseline, 'Interrupted process leaked frames'
        guest.command(b'sync', b'Disk synchronized.')
        guest.send(b'halt\r')
        guest.expect(b'System halted.')
        assert b'PANIC' not in guest.output
        print('PASS: userspace shell, ELF loading, syscall validation, process isolation and preemption', flush=True)
    finally:
        guest.close()


def persistence(args, disk):
    guest = Guest(args, 128, 'system-reboot', disk, reboot=True)
    try:
        guest.boot()
        assert b'Storage: TahaFS persistent /home' in guest.output
        guest.command(b'cat docs/note.txt', b'Bonjour TahaOS\r\nDeuxieme ligne')
        guest.command(b'cat docs/copy.txt', b'Bonjour TahaOS')
        guest.command(b'cat docs/edited.txt', b'Une premiere ligne\r\nUne seconde ligne')
        guest.command(b'run docs/program.elf', b'Hello from an ELF process in ring 3!')
        guest.command(b'cat /tmp/temporary', b'Not found.')
        guest.command(b'rm docs/copy.txt', b'taha>')
        guest.send(b'reboot\r')
        guest.boot()
        guest.command(b'cat docs/copy.txt', b'Not found.')
        guest.send(b'halt\r')
        guest.expect(b'System halted.')
        print('PASS: persistent files and ELF across cold and keyboard-controller reboots; /tmp resets', flush=True)
    finally:
        guest.close()


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--iso', default='build/taha.iso')
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix='system-test-', dir=Path(args.iso).parent) as directory:
        disk = Path(directory) / 'data.img'
        with disk.open('xb') as image:
            image.truncate(16 * 1024 * 1024)
            image.write(b'TAHADK01' + bytes(504))
        session(args, disk)
        persistence(args, disk)

if __name__ == '__main__':
    main()
