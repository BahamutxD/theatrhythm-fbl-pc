"""Write a PS4 module (SELF .prx/.elf) as a flat image of its PT_LOAD and SCE_RELRO segments at their vaddrs.

usage: python tools/flatten_module.py MODULE OUT.bin [NAME ...]
Prints the program headers, then every relocation whose symbol contains one of NAME (NIDs).
A guest offset from a Fault/STOP line minus the module base (out/game/link.json) is an offset
into OUT.bin; disassemble a window with llvm-objcopy -I binary + llvm-objdump (see the
boot-stop-loop skill) or load OUT.bin into Ghidra as raw x86-64 at base 0.
"""
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'scripts'))
from link_modules import module  # noqa: E402


def main():
    m = module(sys.argv[1])
    size = max(p['vaddr'] + p['memsz'] for p in m['ph'] if p['type'] in (1, 0x61000010))
    flat = bytearray(size)
    for p in m['ph']:
        if p['type'] in (1, 0x61000010):
            flat[p['vaddr']:p['vaddr'] + p['filesz']] = m['elf'][p['offset']:p['offset'] + p['filesz']]
            print(f"load vaddr={p['vaddr']:#x} filesz={p['filesz']:#x} memsz={p['memsz']:#x} flags={p['flags']}")
    Path(sys.argv[2]).write_bytes(flat)
    for target, kind, sym, addend in m['relocs']:
        name = m['symbols'][sym]['name'] if sym < len(m['symbols']) else ''
        if any(k in name for k in sys.argv[3:]):
            print(f'reloc {target:#x} type={kind} {name}')


if __name__ == '__main__':
    main()
