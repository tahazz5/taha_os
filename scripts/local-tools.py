#!/usr/bin/env python3
"""Extract Ubuntu/Debian build tools under build/tools without root privileges."""
from pathlib import Path
import subprocess
import os
import shlex

root = Path(__file__).resolve().parents[1] / 'build' / 'tools'
archives = root / 'archives'
sysroot = root / 'root'
bin_dir = root / 'bin'
for directory in (archives, sysroot, bin_dir):
    directory.mkdir(parents=True, exist_ok=True)
requested = ['qemu-system-x86', 'qemu-system-gui', 'qemu-system-data', 'seabios', 'ipxe-qemu', 'xorriso']
plan = subprocess.check_output(['apt-get', '--simulate', '--no-install-recommends', 'install', *requested], text=True)
packages = sorted(set(requested + [line.split()[1] for line in plan.splitlines() if line.startswith('Inst ')]))
subprocess.run(['apt-get', 'download', *packages], cwd=archives, check=True)
for archive in sorted(archives.glob('*.deb')):
    subprocess.run(['dpkg-deb', '-x', str(archive), str(sysroot)], check=True)
# Relocate firmware symlinks that packages normally resolve under /usr/share.
for link in sysroot.rglob('*'):
    if link.is_symlink() and link.readlink().is_absolute():
        target = sysroot / str(link.readlink()).lstrip('/')
        if target.exists():
            relative = os.path.relpath(target, link.parent)
            link.unlink()
            link.symlink_to(relative)
# QEMU's distro defaults also search /usr/share/seabios and /usr/share/ipxe.
# Put those firmware names into our single local -L search directory.
share = sysroot / 'usr/share'
for folder in ('seabios', 'ipxe/qemu'):
    for firmware in (share / folder).glob('*'):
        link = share / 'qemu' / firmware.name
        if firmware.is_file() and not link.exists():
            link.symlink_to(Path('..') / folder / firmware.name)
for name in ('qemu-system-x86_64', 'xorriso'):
    executable = sysroot / 'usr' / 'bin' / name
    # Debian/Ubuntu may place the emulator behind an absolute symlink.
    if executable.is_symlink() and executable.readlink().is_absolute():
        executable = sysroot / str(executable.readlink()).lstrip('/')
    quote = shlex.quote
    flags = f' -L {quote(str(sysroot / "usr/share/qemu"))}' if name.startswith('qemu') else ''
    wrapper = bin_dir / name
    wrapper.write_text('#!/bin/sh\n' +
        f'export LD_LIBRARY_PATH={quote(str(sysroot / "usr/lib/x86_64-linux-gnu") + ":" + str(sysroot / "usr/lib/x86_64-linux-gnu/pulseaudio"))}${{LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}}\n' +
        f'export QEMU_MODULE_DIR={quote(str(sysroot / "usr/lib/x86_64-linux-gnu/qemu"))}\n' +
        f'exec {quote(str(executable))}{flags} "$@"\n')
    wrapper.chmod(0o755)
print(f'Local tools ready: {bin_dir}')
