#!/usr/bin/env python3
"""Create an empty 16 MiB data disk once; never overwrite an existing image."""
from pathlib import Path
import sys
path = Path(sys.argv[1])
path.parent.mkdir(parents=True, exist_ok=True)
try:
    with path.open('xb') as image:
        image.truncate(16 * 1024 * 1024)
        image.write(b'TAHADK01' + bytes(504))
    print(f'Created {path}')
except FileExistsError:
    print(f'Keeping existing {path}')
