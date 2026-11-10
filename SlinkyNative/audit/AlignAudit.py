#!/usr/bin/env python3
"""
AlignAudit.py - check that every aligned SSE access in the built engine points at a
16-byte-aligned address.

The engine keeps its constants in SlinkyNative/src/data/rdata.s, a sequential dump with no
per-item alignment. Inserting a constant that is not a multiple of 16 bytes slides every
later constant down by the remainder, and the 16-byte values declared
__attribute__((vector_size(16), aligned(16))) are read with movaps/movapd/movdqa - which
fault on an address that is not a multiple of 16 instead of reading a wrong number. That is
how v1.3.0 crashed on joining a world: twelve inserted bytes, nine constants, twelve sites.

This checks the shipped binary rather than the source, so it catches the shift whatever
caused it. Run from the repository root:

    python SlinkyNative/audit/AlignAudit.py [path/to/slinky-native.dll] [path/to/slinky-native.map]
    python SlinkyNative/audit/AlignAudit.py [path/to/libslinky-engine.so]

A Linux image needs no map: it carries its own symbol table, which is where the names come from.
It is also the one that reaches its constants through a register, so both spellings of a site count.

Exit status is 0 when every aligned access is aligned, 1 otherwise.
"""

import os
import re
import subprocess
import sys

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
DEFAULT_DLL = os.path.join(REPO, "build", "SlinkyNative", "slinky-native.dll")
DEFAULT_MAP = os.path.join(REPO, "build", "SlinkyNative", "slinky-native.map")

ALIGNED_SSE = ("movaps", "movapd", "movdqa")

# llvm-objdump annotates the RIP-relative target after a '#', e.g.
#   movaps 0x67b73(%rip), %xmm7  # 0x180089d4c <str_getdy+12>
SITE = re.compile(
    r"^\s*([0-9a-f]+):\s"               # instruction address
    r"(?:[0-9a-f]{2}\s+)*"              # raw bytes, when the disassembler prints them
    r"(" + "|".join(ALIGNED_SSE) + r")\s"
    r"(.*)#\s*(0x[0-9a-f]+)",            # operands, then the resolved absolute target
    re.IGNORECASE,
)

# A position-independent image reaches a data symbol it may have to share through a register instead:
#   leaq 0x60aa5(%rip), %rax  # 0xd5390 <math_helpers_fabs_masks>
#   movaps (%rax), %xmm1
# which is how Linux reads the constants of src/data/rdata.s. The address is on the first line and the
# aligned access on the second, so the pair is one site: the lea names the address, the load says it has
# to be 16-byte aligned. Only the instruction straight after the lea counts - nothing can have changed the
# register in between.
LEA_RIP = re.compile(
    r"^\s*[0-9a-f]+:\s(?:[0-9a-f]{2}\s+)*lea[qwbl]?\s+.*\(%rip\),\s*(%\w+)\s*#\s*(0x[0-9a-f]+)",
    re.IGNORECASE,
)
SSE_THROUGH_REG = re.compile(
    r"^\s*([0-9a-f]+):\s"
    r"(?:[0-9a-f]{2}\s+)*"
    r"(" + "|".join(ALIGNED_SSE) + r")\s+"
    r"(?:%\w+\s*,\s*)?"                      # a store names the vector first
    r"(?:(-?0x[0-9a-f]+|-?\d+))?\((%\w+)\)",
    re.IGNORECASE,
)

MAP_SYMBOL = re.compile(
    r"^\s*[0-9a-f]{4}:([0-9a-f]{8})\s+(\S+)\s+([0-9a-f]{16})\s",
    re.IGNORECASE,
)

# llvm-nm on an ELF image: "000000000024ebc0 R str_slinky"
NM_SYMBOL = re.compile(r"^([0-9a-f]{16}) (?:[A-Za-z?]) (\S+)$")


def find_tool(exe_stem):
    """The bundled clang toolchain first, then whatever is on PATH."""
    env = os.environ.get("SLINKY_" + exe_stem.upper().replace("-", "_"))
    if env:
        return env
    pattern = re.compile(r"clang\+llvm-.*")
    bindir = os.path.join(REPO, "tools", "llvm")
    if os.path.isdir(bindir):
        for name in sorted(os.listdir(bindir)):
            if not pattern.match(name):
                continue
            for exe in (exe_stem + ".exe", exe_stem):
                cand = os.path.join(bindir, name, "bin", exe)
                if os.path.isfile(cand):
                    return cand
    return exe_stem


def is_elf(path):
    with open(path, "rb") as handle:
        return handle.read(4) == b"\x7fELF"


def read_symbols(path):
    """(address, name) from the MSVC-format linker map, sorted by address."""
    if not os.path.isfile(path):
        return []
    out = []
    base = 0x180000000  # the image base the linker map is relative to
    with open(path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            m = MAP_SYMBOL.match(line)
            if m:
                out.append((int(m.group(3), 16), m.group(2)))
    out.sort()
    return out


def read_symbols_nm(nm, image):
    """(address, name) from the symbol table an ELF image carries itself."""
    proc = subprocess.run(
        [nm, "--defined-only", image],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.strip() + "\n")
        raise SystemExit("[X] %s could not read symbols of %s" % (nm, image))
    out = []
    for line in proc.stdout.splitlines():
        m = NM_SYMBOL.match(line.strip())
        if m:
            out.append((int(m.group(1), 16), m.group(2)))
    out.sort()
    return out


def symbol_at(symbols, addr):
    """The last symbol at or below addr - the constant the access really points at."""
    lo, hi, best = 0, len(symbols) - 1, None
    while lo <= hi:
        mid = (lo + hi) // 2
        if symbols[mid][0] <= addr:
            best = symbols[mid]
            lo = mid + 1
        else:
            hi = mid - 1
    return best


def disassemble(objdump, dll):
    proc = subprocess.run(
        [objdump, "-d", dll],
        stdout=subprocess.PIPE, stderr=subprocess.PIPE, universal_newlines=True,
    )
    if proc.returncode != 0:
        sys.stderr.write(proc.stderr.strip() + "\n")
        raise SystemExit("[X] %s could not disassemble %s" % (objdump, dll))
    return proc.stdout


def main(argv):
    dll = argv[1] if len(argv) > 1 else DEFAULT_DLL
    if not os.path.isfile(dll):
        raise SystemExit("[X] no engine at %s - build first" % dll)
    objdump = find_tool("llvm-objdump")
    if is_elf(dll):
        symbols = read_symbols_nm(find_tool("llvm-nm"), dll)
    else:
        # Name the constants from the map that belongs to the binary being checked; a map from another
        # build would point at the wrong symbols.
        sibling = os.path.splitext(dll)[0] + ".map"
        map_path = argv[2] if len(argv) > 2 else (sibling if os.path.isfile(sibling) else DEFAULT_MAP)
        symbols = read_symbols(map_path)
    total = 0
    failures = {}

    def check(pc, insn, target):
        nonlocal total
        total += 1
        if target % 16 == 0:
            return
        sym = symbol_at(symbols, target)
        name = sym[1] if sym else "?"
        offset = target - sym[0] if sym else -1
        key = (name, target % 16, offset)
        entry = failures.get(key)
        if entry is None:
            failures[key] = [1, pc, insn]
        else:
            entry[0] += 1

    # (register, address) of the lea on the line before this one, if there was one
    lea = None
    for line in disassemble(objdump, dll).splitlines():
        m = SITE.match(line)
        if m:
            check(m.group(1), m.group(3).strip(), int(m.group(4), 16))
            lea = None
            continue
        m = SSE_THROUGH_REG.match(line)
        if m and lea is not None and m.group(4) == lea[0]:
            disp = int(m.group(3), 0) if m.group(3) else 0
            check(m.group(1), m.group(2) + " " + (m.group(3) or "") + "(" + lea[0] + ")",
                  lea[1] + disp)
            lea = None
            continue
        m = LEA_RIP.match(line)
        lea = (m.group(1), int(m.group(2), 16)) if m else None

    print("aligned SSE constant accesses: %d checked with %s" % (total, os.path.basename(objdump)))
    if not total:
        # Nothing found is not the same as everything passing. The disassembly has to name the address each
        # RIP-relative access lands on - llvm-objdump prints it after a '#', GNU objdump does not print it at
        # all - and without those annotations the gate would read an empty list and report an engine it never
        # looked at as clean.
        print("[X] the disassembly carried no annotated constant accesses, so nothing was checked.")
        print("    this needs llvm-objdump (with llvm-nm for the symbol names); set SLINKY_LLVM_OBJDUMP to it.")
        return 1
    if not failures:
        print("[OK] all of them are 16-byte aligned")
        return 0
    print("[X] %d constant(s) read misaligned:" % len(failures))
    for (name, mod, offset), (count, pc, insn) in sorted(failures.items(), key=lambda kv: -kv[1][0]):
        print("    %s  addr%%16=%d  +%s  uses=%d  first at %s (%s)"
              % (name, mod, offset, count, pc, insn.strip()))
    print("    a constant inserted into src/data/rdata.s is not padded to 16 bytes;")
    print("    pad it, or append after the last one instead of inserting")
    return 1


if __name__ == "__main__":
    sys.exit(main(sys.argv))
