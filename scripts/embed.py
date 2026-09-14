#!/usr/bin/env python3
from pathlib import Path
import json
import sys
output = ['.section .rodata', '.balign 16']
for filename in sys.argv[2:]:
    path = Path(filename).resolve()
    name = path.stem
    if not name.isidentifier():
        raise ValueError('Invalid program name')
    output += [f'.global app_{name}_start, app_{name}_end', f'app_{name}_start:',
               f'.incbin {json.dumps(str(path))}', f'app_{name}_end:', '.balign 16']
output += ['.section .note.GNU-stack,"",@progbits']
Path(sys.argv[1]).write_text('\n'.join(output) + '\n')
