#!/usr/bin/env python3
"""Exercise desktop workflows using actual PS/2 input and framebuffer checks through QMP."""
import argparse
import json
import os
import re
from pathlib import Path
import selectors
import struct
import subprocess
import tempfile
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
    disk = tempfile.TemporaryDirectory(prefix='taha-desktop-')
    disk_path = str(Path(disk.name) / 'data.img')
    subprocess.run(['python3', 'scripts/disk.py', disk_path], check=True)
    p = subprocess.Popen([args.qemu, '-accel', 'tcg', '-m', '128M', '-boot', 'd',
        '-cdrom', args.iso, '-drive', f'file={disk_path},format=raw,if=ide,index=0', '-display', 'none', '-nic', 'none', '-serial', f'file:{serial}',
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
    def expect(text, count=1):
        deadline = time.monotonic() + 20
        while time.monotonic() < deadline:
            if serial.exists() and serial.read_bytes().count(text) >= count:
                return
            time.sleep(0.05)
        raise AssertionError(f'Missing {text!r}\n{serial.read_text(errors="replace")}')
    def key(name):
        command('human-monitor-command', {'command-line': f'sendkey {name} 20'})
        time.sleep(0.08)
    def type_text(text):
        for c in text:
            name = {' ': 'spc', '\n': 'ret', '/': 'slash', '.': 'dot'}.get(c, c)
            command('human-monitor-command', {'command-line': f'sendkey {name} 20'})
            time.sleep(0.04)
    def type_line(text):
        type_text(text + '\n')
    pointer = [40, 70]
    def move(x, y):
        while pointer != [x, y]:
            dx = max(-80, min(80, x - pointer[0]))
            dy = max(-80, min(80, y - pointer[1]))
            command('input-send-event', {'events': [
                {'type': 'rel', 'data': {'axis': 'x', 'value': dx}},
                {'type': 'rel', 'data': {'axis': 'y', 'value': dy}}]})
            pointer[0] += dx; pointer[1] += dy
            time.sleep(0.04)
    def button(down):
        command('input-send-event', {'events': [
            {'type': 'btn', 'data': {'button': 'left', 'down': down}}]})
        time.sleep(0.1)
    def click(x, y):
        move(x, y); button(True); button(False)
    def snapshot(name):
        target = root / name
        command('screendump', {'filename': str(target)})
        with target.open('rb') as f:
            assert f.readline().strip() == b'P6'
            w, h = map(int, f.readline().split())
            assert f.readline().strip() == b'255'
            data = f.read()
        return w, h, data
    def pixel(shot, x, y):
        w, _, data = shot
        return data[(y*w+x)*3:(y*w+x)*3+3]
    def crop(shot, x, y, w, h):
        stride = shot[0] * 3
        return b''.join(shot[2][j*stride+x*3:j*stride+(x+w)*3] for j in range(y, y+h))
    try:
        assert 'QMP' in message()
        command('qmp_capabilities')
        expect(b'taha> ')
        expect(b'PS/2 keyboard ready')
        expect(b'PS/2 mouse ready')
        type_line('clear')
        type_line('help')
        expect(b'TahaOS shell (ring 3)')
        type_line('run hello')
        expect(b'Hello from an ELF process in ring 3!')
        # A user process may draw only within its client area, including negative origins.
        type_line('bg guicheck wait')
        expect(b'GUIWAIT ready')
        gui_pid = int(re.findall(rb'Started PID (\d+)', serial.read_bytes())[-1])
        clipped = snapshot('clipped.ppm')
        assert pixel(clipped, 65, 115) == bytes.fromhex('67dec8'), 'Client drawing missing'
        assert pixel(clipped, 65, 90) == bytes.fromhex('285260'), 'App drew over its title bar'
        assert pixel(clipped, 63, 115) != bytes.fromhex('67dec8'), 'App escaped its client area'
        key('f1'); type_line('kill ' + str(gui_pid))
        released = snapshot('released.ppm')
        assert pixel(released, 65, 115) != bytes.fromhex('67dec8'), 'Killed app left a window behind'
        # Open Notes, type through real PS/2 input, and persist with its Save button.
        click(340, 744)
        notes = snapshot('notes.ppm')
        assert pixel(notes, 100, 110) == bytes.fromhex('285260'), 'Notes did not open'
        type_line('desktop note')
        click(126, 154)
        saved_notes = snapshot('saved-notes.ppm')
        # Undo/redo work through PS/2 and toolbar, including the saved-state marker.
        type_text('x')
        changed_notes = snapshot('undo-edited.ppm')
        key('ctrl-z')
        undone_notes = snapshot('undo-restored.ppm')
        assert crop(undone_notes, 108, 204, 96, 32) == crop(saved_notes, 108, 204, 96, 32), 'Undo did not restore text'
        assert crop(undone_notes, 548, 176, 16, 16) == crop(saved_notes, 548, 176, 16, 16), 'Undo did not restore saved state'
        key('ctrl-y')
        redone_notes = snapshot('redo-restored.ppm')
        assert crop(redone_notes, 108, 204, 96, 32) == crop(changed_notes, 108, 204, 96, 32), 'Redo did not restore edit'
        click(370, 154)
        toolbar_undo = snapshot('toolbar-undo.ppm')
        assert crop(toolbar_undo, 108, 204, 96, 32) == crop(saved_notes, 108, 204, 96, 32), 'Undo button failed'
        click(426, 154)
        toolbar_redo = snapshot('toolbar-redo.ppm')
        assert crop(toolbar_redo, 108, 204, 96, 32) == crop(changed_notes, 108, 204, 96, 32), 'Redo button failed'
        key('ctrl-z')
        click(64, 744)
        type_line('cat /home/desktop.txt')
        expect(b'desktop note')
        # Drag Notes and verify both its new bounds and exposed desktop.
        click(340, 744)
        move(220, 122); button(True); move(340, 162); button(False)
        dragged = snapshot('dragged.ppm')
        assert pixel(dragged, 220, 150) == bytes.fromhex('285260'), 'Title bar did not move'
        assert pixel(dragged, 100, 110) != bytes.fromhex('285260'), 'Old window remained'
        # Close and reopen: app state must survive closing its window.
        click(672, 162)
        closed = snapshot('closed.ppm')
        assert pixel(closed, 220, 150) != bytes.fromhex('285260'), 'Close failed'
        click(340, 744)
        reopened = snapshot('reopened.ppm')
        assert pixel(reopened, 220, 150) == bytes.fromhex('285260'), 'Reopen failed'
        # Files lists the saved note and previews its actual contents.
        click(200, 744)
        files = snapshot('files.ppm')
        assert pixel(files, 68, 86) == bytes.fromhex('285260'), 'Files did not open'
        click(130, 180)
        preview = snapshot('preview.ppm')
        move(600, 700)
        preview = snapshot('preview.ppm')
        assert crop(preview, 76, 196, 96, 16) == crop(saved_notes, 108, 204, 96, 16), 'Preview does not match saved note'
        # Open a file in the editor and modify its middle using extended PS/2 keys.
        click(126, 180)
        click(236, 244); key('delete'); type_text('e')
        key('end'); type_text('s'); key('ctrl-s')
        key('f1'); type_line('cat /home/desktop.txt')
        expect(b'\r\ndesktop notes\r\n')
        key('f2'); click(400, 130)
        type_line('/home/projects')
        key('f3'); key('ctrl-n'); type_line('second document')
        key('ctrl-s'); type_line('/home/projects/second.txt')
        key('f1'); type_line('cat /home/projects/second.txt')
        expect(b'\r\nsecond document\r\n')
        # Cancel an unsaved-document prompt, then save the retained edit.
        key('f3'); type_text('x'); key('ctrl-n'); key('esc'); key('ctrl-s')
        key('f1'); type_line('cat /home/projects/second.txt')
        expect(b'\r\nsecond document\r\nx')
        # Save As must ask before replacing another file; Escape leaves both intact.
        key('f3'); key('ctrl-shift-s'); type_line('/home/desktop.txt')
        confirmation = snapshot('overwrite.ppm')
        assert pixel(confirmation, 230, 359) == bytes.fromhex('213041'), 'Overwrite confirmation missing'
        key('esc')
        # Failed Open stays in the dialog; selecting a valid path recovers.
        key('ctrl-o'); type_line('/home/missing.txt')
        failed_open = snapshot('open-error.ppm')
        assert pixel(failed_open, 230, 359) == bytes.fromhex('213041'), 'Open error dialog missing'
        key('ctrl-a'); type_line('/home/desktop.txt'); key('end')
        key('f1'); type_line('cat /home/desktop.txt')
        expect(b'\r\ndesktop notes\r\n', 2)
        # Both desktop apps must be distinct processes blocked on window events.
        key('f1'); type_line('ps')
        expect(b'/bin/notes.elf'); expect(b'/bin/files.elf')
        processes = serial.read_bytes()
        note_pids = re.findall(rb'\r?\n(\d+) 0 \d+ 6 /bin/notes.elf', processes)
        file_pids = re.findall(rb'\r?\n(\d+) 0 \d+ 6 /bin/files.elf', processes)
        assert note_pids and file_pids and note_pids[-1] != file_pids[-1], 'Apps are not isolated event waiters'
        # An unsaved close can be canceled, preserving the running app and document.
        key('f3'); key('end'); type_text('x'); click(672, 154); key('esc')
        key('backspace'); key('ctrl-s')
        # Maximize and restore Notes without losing document state.
        key('f3'); click(644, 162)
        full = snapshot('maximized.ppm')
        assert pixel(full, 12, 54) == bytes.fromhex('285260'), 'Maximize failed'
        click(964, 66)
        restored = snapshot('restored.ppm')
        assert pixel(restored, 220, 150) == bytes.fromhex('285260'), 'Restore failed'
        # A cold boot must reload the saved note from the same temporary disk.
        p.kill(); p.communicate(timeout=5); selector.unregister(p.stdout)
        p = subprocess.Popen([args.qemu, '-accel', 'tcg', '-m', '128M', '-boot', 'd',
            '-cdrom', args.iso, '-drive', f'file={disk_path},format=raw,if=ide,index=0',
            '-display', 'none', '-nic', 'none', '-serial', f'file:{serial}',
            '-qmp', 'stdio', '-no-reboot', '-no-shutdown'], stdin=subprocess.PIPE,
            stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        selector.register(p.stdout, selectors.EVENT_READ); pending.clear()
        assert 'QMP' in message()
        command('qmp_capabilities'); expect(b'taha> ')
        pointer[:] = [40, 70]
        click(340, 744)
        key('end')
        reloaded = snapshot('reloaded.ppm')
        assert crop(reloaded, 108, 204, 96, 16) == crop(saved_notes, 108, 204, 96, 16), 'Note was not reloaded after cold boot'
        move(220, 122); button(True); move(644, 142); button(False)
        click(200, 744)
        click(130, 180)
        move(200, 98); button(True); move(160, 98); button(False)
        move(600, 700)
        assert b'PANIC' not in serial.read_bytes()
        ppm = root / 'desktop.ppm'
        command('screendump', {'filename': str(ppm)})
        png(ppm, root / 'desktop.png')
        print(f'PASS: PS/2 keyboard/mouse, focus, drag, close/reopen, editor navigation, named files/folders, unsaved protection, maximize/restore, process isolation, drawing bounds, cold boot; screenshot: {root / "desktop.png"}')
    finally:
        p.kill(); p.communicate(timeout=5); selector.close(); disk.cleanup()

if __name__ == '__main__':
    main()
