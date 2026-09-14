#!/usr/bin/env python3
"""Exercise the actual PS/2 keyboard and capture the framebuffer through QMP."""
import argparse
import json
import os
from pathlib import Path
import selectors
import struct
import subprocess
import time
import zlib


def png(ppm, output):
    with ppm.open('rb') as image:
        assert image.readline().strip() == b'P6'
        dimensions = image.readline()
        while dimensions.startswith(b'#'):
            dimensions = image.readline()
        width, height = map(int, dimensions.split())
        assert image.readline().strip() == b'255'
        pixels = image.read()
    assert width >= 640 and height >= 480 and len(pixels) == width * height * 3
    assert pixels[:3] == bytes.fromhex('182431'), 'Expected TahaOS header pixels'
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    rows = b''.join(b'\0' + pixels[y * width * 3:(y + 1) * width * 3] for y in range(height))
    output.write_bytes(b'\x89PNG\r\n\x1a\n' +
        chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0)) +
        chunk(b'IDAT', zlib.compress(rows)) + chunk(b'IEND', b''))


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--qemu', default='qemu-system-x86_64')
    parser.add_argument('--iso', default='build/taha.iso')
    args = parser.parse_args()
    root = Path(args.iso).resolve().parent
    serial = root / 'display-serial.log'
    p = subprocess.Popen([args.qemu, '-accel', 'tcg', '-m', '128M', '-boot', 'd',
        '-cdrom', args.iso, '-display', 'none', '-nic', 'none', '-serial', f'file:{serial}',
        '-qmp', 'stdio', '-no-reboot', '-no-shutdown'], stdin=subprocess.PIPE,
        stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    selector = selectors.DefaultSelector()
    selector.register(p.stdout, selectors.EVENT_READ)
    pending = bytearray()
    def message():
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if b'\n' in pending:
                line, _, rest = pending.partition(b'\n'); pending[:] = rest
                return json.loads(line)
            if selector.select(0.1):
                data = os.read(p.stdout.fileno(), 65536)
                if not data:
                    raise AssertionError(p.stderr.read().decode())
                pending.extend(data)
        raise AssertionError('QMP timeout')
    def command(execute, arguments=None):
        request = {'execute': execute, 'id': execute}
        if arguments is not None:
            request['arguments'] = arguments
        p.stdin.write(json.dumps(request).encode() + b'\n'); p.stdin.flush()
        while True:
            reply = message()
            if reply.get('id') == execute:
                assert 'error' not in reply, reply
                return reply
    def expect(text):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if serial.exists() and text in serial.read_bytes():
                return
            time.sleep(0.05)
        raise AssertionError(f'Missing {text!r}\n{serial.read_text(errors="replace")}')
    def type_line(text):
        for c in text + '\n':
            key = {' ': 'spc', '\n': 'ret'}.get(c, c)
            command('human-monitor-command', {'command-line': f'sendkey {key} 20'})
            time.sleep(0.04)
    try:
        assert 'QMP' in message()
        command('qmp_capabilities')
        expect(b'taha> ')
        expect(b'PS/2 keyboard ready')
        type_line('clear')
        type_line('help')
        expect(b'TahaOS shell (ring 3)')
        type_line('run hello')
        expect(b'Hello from an ELF process in ring 3!')
        assert b'PANIC' not in serial.read_bytes()
        ppm = root / 'desktop.ppm'
        command('screendump', {'filename': str(ppm)})
        png(ppm, root / 'desktop.png')
        print(f'PASS: framebuffer, PS/2 input, userspace shell; screenshot: {root / "desktop.png"}')
    finally:
        p.kill(); p.communicate(timeout=5); selector.close()

if __name__ == '__main__':
    main()
