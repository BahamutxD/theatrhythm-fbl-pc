"""Name IL2CPP code: C# method of every method pointer in Il2CppUserAssemblies.prx.

usage: python tools/il2cpp_symbols.py GAME_DIR OUT.tsv [ADDRESS ...]
Writes "offset<TAB>Namespace.Type::Method" lines (module offsets, sorted) and prints the
method containing each ADDRESS (a module offset, hex). Addresses from a Fault line are guest
offsets: subtract the module base from out/game/link.json first.

Global metadata v29 (Unity 2021.2): methods are 32-byte records, types 88, images 40. Each image
(assembly) has an Il2CppCodeGenModule in the module's data, found through the relocation that
points its first field at the assembly's file name; methodPointers[rid - 1] is the code of the
method with token rid.
"""
import bisect
import struct
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent.parent / 'scripts'))
from link_modules import find_module, module  # noqa: E402

SECTIONS = ('string_literal', 'string_literal_data', 'string', 'events', 'properties', 'methods')


def metadata(path):
    d = Path(path).read_bytes()
    sanity, version = struct.unpack_from('<Ii', d, 0)
    if sanity != 0xFAB11BAF or version != 29:
        raise ValueError(f'unsupported metadata (version {version})')
    pairs = struct.unpack_from('<64I', d, 8)
    section = lambda i: (pairs[2 * i], pairs[2 * i + 1])  # noqa: E731
    strings = section(2)

    def string(index):
        start = strings[0] + index
        return d[start:d.index(0, start)].decode('utf8', 'replace')
    methods_at, methods_size = section(5)
    types_at, types_size = section(19)
    images_at, images_size = section(20)
    if methods_size % 32 or types_size % 88 or images_size % 40:
        raise ValueError('unexpected record sizes')
    methods = [struct.unpack_from('<6I4H', d, methods_at + i * 32) for i in range(methods_size // 32)]
    types = [struct.unpack_from('<8I8I8H2I', d, types_at + i * 88) for i in range(types_size // 88)]
    images = [struct.unpack_from('<10I', d, images_at + i * 40) for i in range(images_size // 40)]
    return string, methods, types, images


def symbols(game):
    string, methods, types, images = metadata(game / 'Media/Metadata/global-metadata.dat')
    m = module(find_module(game, 'Il2CppUserAssemblies.prx'))
    flat = bytearray(max(p['vaddr'] + p['memsz'] for p in m['ph'] if p['type'] in (1, 0x61000010)))
    for p in m['ph']:
        if p['type'] in (1, 0x61000010):
            flat[p['vaddr']:p['vaddr'] + p['filesz']] = m['elf'][p['offset']:p['offset'] + p['filesz']]
    pointer = {target: addend for target, kind, sym, addend in m['relocs'] if kind == 8}  # R_X86_64_RELATIVE
    by_value = {}
    for target, addend in pointer.items():
        by_value.setdefault(addend, []).append(target)
    out = []
    for image in images:
        name = string(image[0]).encode() + b'\0'
        type_start, type_count = image[2], image[3]
        candidates = []
        start = 0
        while (at := flat.find(name, start)) >= 0:  # the string may follow data, not only a NUL
            candidates += by_value.get(at, [])
            start = at + 1
        cgm = next((c for c in candidates if pointer.get(c + 16)), None)  # moduleName, count, methodPointers
        if cgm is None:
            continue
        count, = struct.unpack_from('<I', flat, cgm + 8)
        table = pointer[cgm + 16]
        for t in types[type_start:type_start + type_count]:
            type_name = string(t[0])
            namespace = string(t[1])
            full = f'{namespace}.{type_name}' if namespace else type_name
            method_start, method_count = t[9], t[16]
            for md in methods[method_start:method_start + method_count]:
                rid = md[5] & 0xFFFFFF
                if not 0 < rid <= count:
                    continue
                code = pointer.get(table + (rid - 1) * 8)
                if code:
                    out.append((code, f'{full}::{string(md[0])}'))
    return sorted(set(out))


def main():
    game, out = Path(sys.argv[1]), Path(sys.argv[2])
    table = symbols(game)
    with out.open('w', newline='') as f:
        for address, name in table:
            f.write(f'{address:#x}\t{name}\n')
    print(f'{len(table)} methods -> {out}')
    starts = [a for a, _ in table]
    for text in sys.argv[3:]:
        address = int(text, 16)
        i = bisect.bisect_right(starts, address) - 1
        print(f'{address:#x}: ' + (f'{table[i][1]} +{address - starts[i]:#x}' if i >= 0 else 'before the first method'))


if __name__ == '__main__':
    main()
