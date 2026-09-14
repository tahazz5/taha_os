#!/usr/bin/env python3
"""Check the load contract, including permissions, rather than only compilation."""
import struct
import subprocess
import sys
from pathlib import Path

path = Path(sys.argv[1])
data = path.read_bytes()
assert data[:7] == b'\x7fELF\x02\x01\x01', 'Expected little-endian ELF64'
kind, machine = struct.unpack_from('<HH', data, 16)
assert (kind, machine) == (2, 62), 'Expected executable x86-64 ELF'
entry, phoff = struct.unpack_from('<QQ', data, 24)
phsize, phnum = struct.unpack_from('<HH', data, 54)
assert phsize == 56
loads = []
for i in range(phnum):
    ptype, flags, offset, virtual, physical, filesz, memsz, align = struct.unpack_from('<IIQQQQQQ', data, phoff + i * phsize)
    assert ptype not in (2, 3), 'Dynamic loader/runtime is forbidden'
    if ptype == 1:
        assert flags & 3 != 3, 'Writable executable segment'
        assert filesz <= memsz and offset + filesz <= len(data)
        assert align == 4096 and virtual % align == offset % align
        loads.append((virtual, virtual + memsz, flags))
assert any(start <= entry < end and flags & 1 for start, end, flags in loads)
for left, right in zip(sorted(loads), sorted(loads)[1:]):
    assert left[1] <= right[0], 'Overlapping load segments'
assert not subprocess.check_output(['nm', '-u', str(path)]).strip(), 'Unresolved symbols'
symbols = subprocess.check_output(['nm', '-n', str(path)], text=True)
assert any(line.split() == [f'{entry:016x}', 'T', 'kentry'] for line in symbols.splitlines())
assert '_GLOBAL__sub_I' not in symbols, 'Unsupported dynamic constructor'
print('ELF checks passed (entry, architecture, static linkage, segment permissions)')
