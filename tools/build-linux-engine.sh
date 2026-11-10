#!/usr/bin/env bash
# slinkyFX - build the native engine for Linux as build/linux-engine/libslinky-engine.so.
#
# This is the same engine the Windows build produces (src/engine, src/logic, src/modules, NanoVG, the
# assembled data blobs), compiled for x86_64 Linux, by one of two toolchains:
#
#   from Windows  the bundled clang plus a downloaded sysroot, and a link against an old glibc, so the one
#                 file runs on every distribution the README lists. Needs tools/setup-linux-sysroot.sh:
#                   .sysroot/linux        glibc, libstdc++, kernel and X11 headers (Debian 13 debs)
#                   .sysroot/glibc-2.31   the runtime libraries the link resolves against (Ubuntu 20.04)
#   on Linux      the machine's own clang, lld and headers - no sysroot. This is the path a player takes when
#                 build.sh runs on their box, and the file it makes is theirs alone: a source build has always
#                 been allowed to need the glibc it was built on, because it never has to run anywhere else.
#
# Why two sysroots for the cross build: the *headers* come from the current Debian release, the *link-time
# libraries* from glibc 2.31. What a shared object needs of libc is recorded in it as versioned references
# (fmod@GLIBC_2.27), and the loader refuses to start an image whose newest reference is newer than the libc it
# lands on. Linking against 2.31 keeps the newest reference at 2.29, so one file loads on Debian 11/12/13,
# Ubuntu 20.04/22.04/24.04, every current Fedora and Arch - and the linker proves it, because a symbol that
# does not exist in 2.31 fails the link instead of failing in someone's game.
#
# The cross link is deliberately not the compiler driver's: the bundled LLVM is a Windows toolchain and has no
# Linux GCC installation next to it, so there is no crtbeginS.o, libstdc++ or libgcc to find. None are needed -
# the engine is -fno-exceptions -fno-rtti C++ over the C library.
#
# Usage (from the repository root, Git Bash on Windows or any Linux shell):
#   tools/build-linux-engine.sh            build, check, leave the file in build/linux-engine
#   tools/build-linux-engine.sh -Clean     throw the objects away first
#   tools/build-linux-engine.sh -Dist      ... and also copy to dist/
#   SLINKY_CROSS_BUILD=1 tools/build-linux-engine.sh   on Linux: use .sysroot and the bundled LLVM anyway
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
ENGINE="$ROOT/SlinkyNative"
SR="$ROOT/.sysroot/linux"
LEGACY="$ROOT/.sysroot/glibc-2.31/lib/x86_64-linux-gnu"
OUT="$ROOT/build/linux-engine"
JOBS="${JOBS:-10}"

CLEAN=0; DIST=0
for arg in "$@"; do
    case "$arg" in
        -Clean|--clean) CLEAN=1 ;;
        -Dist|--dist)   DIST=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

BIN=""
for cand in "$ROOT"/tools/llvm/clang+llvm-*/bin; do
    [[ -x "$cand/clang++" || -f "$cand/clang++.exe" ]] && BIN="$cand" && break
done

exe() { # a tool name, with the .exe the Windows build of LLVM uses
    if [[ -f "$BIN/$1.exe" ]]; then printf '%s/%s.exe' "$BIN" "$1"; else printf '%s/%s' "$BIN" "$1"; fi
}

# Two machines build this file, and they are not the same machine.
#
# Cross (the Windows path): the bundled LLVM, a downloaded Debian sysroot for the headers and an old Ubuntu
# glibc for the link, so one .so serves every distribution on the list.
#
# Native (a player who follows the README and builds slinkyFX on their own box): their clang, their headers,
# their glibc. The compiler has to stay clang - the engine was written, ported and verified against it, the
# warning set below is clang's, and the alignment gate further down reads clang's disassembly. The runtime is
# the machine's own, which is what a source build has always meant: no floor to hold down, because the file
# never leaves the machine that made it.
if [[ "$(uname -s)" == "Linux" && "${SLINKY_CROSS_BUILD:-0}" != "1" ]]; then
    NATIVE=1
    missing=""
    for t in clang clang++ ld.lld llvm-readelf llvm-objdump llvm-nm; do
        command -v "$t" >/dev/null 2>&1 || missing="$missing $t"
    done
    if [[ -n $missing ]]; then
        echo "[X] building the engine needs clang and the LLVM tools; these are missing:$missing" >&2
        echo "    Debian / Ubuntu: sudo apt install clang lld llvm" >&2
        echo "    Fedora:          sudo dnf install clang lld llvm" >&2
        echo "    Arch:            sudo pacman -S clang lld llvm" >&2
        exit 1
    fi
    CLANG=clang; CLANGXX=clang++; LLD=ld.lld; RM_READELF=llvm-readelf
else
    NATIVE=0
    if [[ -z "$BIN" ]]; then
        echo "[X] no bundled LLVM in tools/llvm - run tools/setup-llvm.sh first" >&2
        exit 1
    fi
    CLANG=$(exe clang); CLANGXX=$(exe clang++)
    LLD=$(exe ld.lld); RM_READELF=$(exe llvm-readelf)
fi

if [[ $NATIVE -eq 0 ]]; then
    if [[ ! -d "$SR/usr/include" ]]; then
        echo "[X] no Linux sysroot in .sysroot/linux - run tools/setup-linux-sysroot.sh first" >&2
        exit 1
    fi
    LIBDIR="$SR/usr/lib/x86_64-linux-gnu"
    LINK_LIBDIR="$LIBDIR"
    if [[ -d "$LEGACY" ]]; then
        LINK_LIBDIR="$LEGACY"
    else
        echo "[!] .sysroot/glibc-2.31 is missing: linking against the sysroot's own glibc," >&2
        echo "    which raises the glibc this file needs. Run tools/setup-linux-sysroot.sh." >&2
    fi
    # the sysroot's libc.so is a linker script that opens /lib/x86_64-linux-gnu/libc.so.6, and a deb tree
    # extracted without symlinks has no /lib; naming the files by soname reads the real libraries.
    LIBS=(-l:libc.so.6 -l:libm.so.6 -l:libpthread.so.0 -l:libdl.so.2 -l:librt.so.1 -l:ld-linux-x86-64.so.2)
    TARGET="--target=x86_64-unknown-linux-gnu --sysroot $SR -isystem $SR/usr/include
-isystem $SR/usr/include/x86_64-linux-gnu"
else
    LIBDIR="" LINK_LIBDIR="" LIBS=()
    # Their compiler already knows where its own headers and C runtime are.
    TARGET=""
fi

[[ $CLEAN -eq 1 && -d $OUT ]] && rm -rf "$OUT"
mkdir -p "$OUT"

# -ffile-prefix-map because stb_image.h and stb_truetype.h put __FILE__ in their assert strings, and the engine
# has the third_party files inside NanoVG's own directory: without the map, two of those Windows paths sit in
# the shipped .so, and a player's log could print one. The flag is applied to every source, so nothing that
# comes after it has to be thought about again.
# -fno-builtin-wcslen because wchar_t is 2 bytes here (-fshort-wchar) while glibc's wcslen counts 4-byte units:
# clang recognises the "walk until a zero element" shape and turns our own wide-length loop into a jump to
# libc's, which then reads UTF-16 as UTF-32. The gate at the end of this script refuses the import outright.
BASE="$TARGET -fPIC -fshort-wchar -fdeclspec -fno-builtin-wcslen -O2
-fno-exceptions -fno-rtti -ffile-prefix-map=$ROOT/= -I$ENGINE/include -I$ENGINE/src
-I$ENGINE/third_party/nanovg/src -I$ENGINE/third_party/gl -DAPIENTRY="
# The same opt-in warning set as the Windows engine target: this code reads Java objects as (char *)base +
# offset, so the style families fire thousands of times and bury the mistakes that actually break the game.
# -Wreturn-type-c-linkage is off because the by-value structs here carry a Linux-only trivial destructor (see
# audit/AbiSysv.py) that makes them non-POD on purpose; nothing calls them from C.
WARN="-Wno-everything -Wunused -Wuninitialized -Winit-self -Wreturn-type -Wswitch -Wswitch-bool -Wformat
-Wformat-security -Warray-bounds -Wstringop-overflow -Wstringop-truncation -Wsizeof-array-argument
-Wsizeof-pointer-memaccess -Wnonnull -Wnonnull-compare -Wtautological-compare
-Wtautological-constant-out-of-range-compare -Wpointer-bool-conversion -Wbool-conversion
-Wconditional-uninitialized -Wimplicit-fallthrough -Wunused-result -Wdelete-non-virtual-dtor
-Wmismatched-tags -Woverloaded-virtual -Wshadow
-Wno-return-type-c-linkage"

# ---- sources: the engine's own, plus the Linux platform layer ----
# src/engine/protected/input.cpp is the Win32 window-subclassing input path; src/platform/linux/input.cpp is
# its counterpart, reading the state LWJGL measured in the frame instead of a window procedure.
find "$ENGINE/src/engine" "$ENGINE/src/logic" "$ENGINE/src/modules" -name '*.cpp' \
    ! -path "$ENGINE/src/engine/protected/input.cpp" > "$OUT/sources.list"
ls "$ENGINE"/src/platform/*.cpp "$ENGINE"/src/platform/linux/*.cpp >> "$OUT/sources.list"
# nanovg_impl.c is the GL3 backend, nanovg_gl2_impl.c the GLSL-1.10 one gui_init falls back to on a 2.1 driver
echo "$ENGINE/src/platform/nanovg_impl.c $ENGINE/src/platform/nanovg_gl2_impl.c $ENGINE/third_party/nanovg/src/nanovg.c" \
    | tr ' ' '\n' >> "$OUT/sources.list"
N_SRC=$(grep -c . "$OUT/sources.list")

compile() {
    src="$1"
    obj="$OUT/$(echo "${src##*SlinkyNative/}" | tr '/.' '__').o"
    # incremental: a rebuilt source is older than nothing here, so compare timestamps and move on. The two
    # stamps are what make that safe: a timestamp says nothing about the flags a file was built with, and
    # nothing about the headers it includes - and most of what this image gets wrong first appears in a header.
    # Without the stamps a changed BASE or an edited include would keep every object from the previous build,
    # and the gates below would measure a file that is not the current source.
    if [[ -f "$obj" && "$obj" -nt "$src" && "$obj" -nt "$OUT/flags.stamp" && "$obj" -nt "$OUT/headers.stamp" ]]; then
        return 0
    fi
    case "$src" in
        *.cpp) "$CC" $BASE -std=gnu++17 -c $WARN -D_GNU_SOURCE=1 "$src" -o "$obj" 2>&1 | sed "s|^|[$src] |" ;;
        *)     "$CC_C" $BASE -std=gnu11 -c -Wno-everything "$src" -o "$obj" 2>&1 | sed "s|^|[$src] |" ;;
    esac
    test -f "$obj"
}
printf '%s\n%s\n' "$BASE" "$WARN" > "$OUT/flags.stamp"
# Moving the stamp is the whole refresh: every object that predates it is rebuilt, which is what "some header
# changed somewhere" means when nothing in the build tracks which source includes which header.
if [[ ! -f "$OUT/headers.stamp" ]] ||
   [[ -n $(find "$ENGINE/include" "$ENGINE/src" "$ENGINE/third_party" \
        \( -name '*.h' -o -name '*.hpp' \) -newer "$OUT/headers.stamp" -print -quit 2>/dev/null) ]]; then
    touch "$OUT/headers.stamp"
fi
export -f compile
export OUT BASE WARN CC="$CLANGXX" CC_C="$CLANG"

echo "[1/4] Compiling $N_SRC sources for x86_64-linux-gnu..."
xargs -P "$JOBS" -a "$OUT/sources.list" -I{} bash -c 'compile "$@"' _ {} > "$OUT/compile.log" 2>&1 || {
    echo "[X] a source failed to build:" >&2; grep -m 20 "error:" "$OUT/compile.log" >&2; exit 1; }
N_OBJ=$(ls "$OUT"/*.o 2>/dev/null | grep -v "/data_" | wc -l) || N_OBJ=0
if [[ $N_OBJ -lt $N_SRC ]]; then
    echo "[X] $N_SRC sources, $N_OBJ objects - see $OUT/compile.log" >&2; exit 1
fi
if grep -q "warning:" "$OUT/compile.log"; then
    echo "[!] $(grep -c 'warning:' "$OUT/compile.log") warning(s):" >&2; grep -m 20 "warning:" "$OUT/compile.log" >&2
    exit 1
fi
echo "      $N_OBJ objects, no warnings"

# ---- the image's own data: constants, globals and the import slots, assembled ----
# COFF puts a section's flags in a string, ELF wants them separate; src/data/*.s carry both spellings under
# the same # ifdef. clang only runs the preprocessor over a .s when the argument list says so, so the flag has
# to be explicit here - without it the guards are comments, both section lines reach the assembler, and it
# takes the second one as a syntax error. The Windows target passes the same flag (SlinkyNative/CMakeLists.txt).
echo "[2/4] Assembling the data blobs..."
for part in rdata data iat; do
    "$CLANG" --target=x86_64-unknown-linux-gnu -x assembler-with-cpp \
        -c "$ENGINE/src/data/$part.s" -o "$OUT/data_$part.o" || { echo "[X] $part.s" >&2; exit 1; }
done

# ---- link ----
# src/engine/aliases.cpp names the engine's internal renames as MSVC "/alternatename" pragmas, which an ELF
# linker ignores; the same list becomes defsym arguments here, so that file stays the only place they live.
ALIASES=$(grep -o '/alternatename:[A-Za-z0-9_]*=[A-Za-z0-9_]*' "$ENGINE/src/engine/aliases.cpp" |
          sed 's|/alternatename:||')
if [[ -z "$ALIASES" ]]; then
    echo "[X] no aliases read from src/engine/aliases.cpp" >&2; exit 1
fi
echo "[3/4] Linking libslinky-engine.so..."
# Everything the bootstrap looks up, and nothing else. By default an ELF shared object exports every name it
# contains - this one had 8786 - and each exported global stays "preemptible", so the code has to reach it
# through its GOT slot instead of in one instruction, the load has to relocate every one of those slots, and a
# library loaded earlier in the JVM can answer the name first. Listing them makes the rest local, which fixes
# all three and, for the alignment gate below, is what actually matters: a constant the code reads as
# "movaps (%rax)" with the address coming out of a GOT slot has no target in the disassembly, so the gate could
# not see the 16-byte SIMD masks that live in src/data/rdata.s - the exact constants whose misalignment
# crashed 1.3.0 in-world. Local symbols are addressed directly, so their addresses are in the listing again.
EXPORTS=(slinky_engine_exited engine_disarm_step engine_leave_and_free slinky_gl_get_proc slinky_log_write)
{
    printf '{\n  global:\n'
    for sym in "${EXPORTS[@]}"; do printf '    %s;\n' "$sym"; done
    printf '  local:\n    *;\n};\n'
} > "$OUT/export.map"
# --no-undefined: every symbol has to be there now. An image with a hole dlopen's fine and then dies the first
# time the code path that needs it runs, which is the worst way to find out.
# -z now: the hook tables and the OpenGL slots are GOT references, so bind them all at load and fail the dlopen
# on a missing one rather than lazily, mid-frame.
# --build-id=none: no random note, so two builds of the same sources are byte-identical and "did that change
# anything?" stays answerable.
if [[ $NATIVE -eq 0 ]]; then
    # The link is deliberately not the compiler driver's: the bundled LLVM is a Windows toolchain and has no
    # Linux GCC installation next to it, so there is no crtbeginS.o, libstdc++ or libgcc to find. None are
    # needed - the engine is -fno-exceptions -fno-rtti C++ over the C library - and the two start-up objects
    # come from the sysroot by hand.
    DEFSYM=$(sed 's/^/--defsym /' <<<"$ALIASES")
    "$LLD" -shared --sysroot="$SR" -L"$LINK_LIBDIR" -L"$LIBDIR" \
        --soname=libslinky-engine.so --no-undefined -z now --build-id=none \
        --version-script="$OUT/export.map" \
        $DEFSYM \
        -o "$OUT/libslinky-engine.so" \
        "$LIBDIR/crti.o" "$OUT"/*.o "$LIBDIR/crtn.o" \
        "${LIBS[@]}"
else
    # On their machine the driver knows the C runtime, so it does the link; lld stays the linker, and the
    # options are handed to it through the driver. -nostdlib++ because nothing here uses libstdc++, and the
    # driver would otherwise pull in versioned references to the player's own C++ runtime for nothing.
    DEFSYM=()
    while read -r alias; do DEFSYM+=(-Wl,--defsym,"$alias"); done <<<"$ALIASES"
    "$CLANGXX" -shared -fPIC -fuse-ld=lld -nostdlib++ \
        -Wl,--soname=libslinky-engine.so -Wl,--no-undefined -Wl,-z,now -Wl,--build-id=none \
        -Wl,--version-script="$OUT/export.map" \
        "${DEFSYM[@]}" \
        -o "$OUT/libslinky-engine.so" \
        "$OUT"/*.o \
        -lm -lpthread -ldl -lrt
fi

# ---- what the bootstrap relies on being in the file ----
# A here-string rather than a pipe: with grep -q the reader stops at the first match, a pipe then kills the
# writer with SIGPIPE, and pipefail would turn "found it" into "missing".
DYNSYM=$("$RM_READELF" --dyn-syms "$OUT/libslinky-engine.so")
DYNAMIC=$("$RM_READELF" -d "$OUT/libslinky-engine.so")
# The export table has to be exactly the list above: a symbol missing is a dlsym that returns NULL in the
# bootstrap, and a symbol present that nobody asked for means the map stopped being applied, which would leave
# the alignment gate further down blind to the blob constants again without saying so.
WANT=$(printf '%s\n' "${EXPORTS[@]}" | sort)
GOT=$(awk 'NF==8 && $2 ~ /^[0-9a-f]{16}$/ && $7!="UND" {print $8}' <<<"$DYNSYM" | sort)
if [[ "$WANT" != "$GOT" ]]; then
    comm -23 <(printf '%s\n' "$WANT") <(printf '%s\n' "$GOT") | sed 's/^/[X] not exported: /' >&2
    comm -13 <(printf '%s\n' "$WANT") <(printf '%s\n' "$GOT") | sed 's/^/[X] exported unasked: /' >&2
    exit 1
fi

# wchar_t is 2 bytes in this image, so every wide string it holds is UTF-16 while the C library reads 4-byte
# units. Any libc wide entry point here would therefore count the length of a path or a class name at roughly
# half the characters, and read on past its end. Nothing may import one - and the compiler has to be held to
# that, because its loop-idiom pass turns our own 2-byte length loop into a jump to wcslen unless BASE says
# otherwise.
WIDE=$(awk '$7=="UND" && $8 ~ /^(wcs|wmem|btowc|wctob|wcstombs|mbsrtowcs|wcsrtombs|v?wc(s?len|scpy|cmp|ncmp))/ {print $8}' <<<"$DYNSYM")
if [[ -n "$WIDE" ]]; then
    echo "[X] the engine asks the C library for a wide-character function, and this build's wchar_t is 2 bytes:" >&2
    printf '%s\n' "$WIDE" | sed 's/^/      /' >&2
    exit 1
fi
if grep -q TEXTREL <<<"$DYNAMIC"; then
    echo "[X] the image asks for a writable text segment (TEXTREL): the blobs' absolute pointers" >&2
    echo "    have to live in .data.rel.ro, see src/data/rdata.s" >&2; exit 1
fi
FLOOR=$("$RM_READELF" -V "$OUT/libslinky-engine.so" | grep -oE 'GLIBC_[0-9.]+' | sort -u -t_ -k2 -V | tail -1)
if [[ $NATIVE -eq 0 ]]; then
    echo "      glibc needed: $FLOOR"
else
    echo "      glibc needed: $FLOOR (this machine's own - a source build never travels)"
fi
echo "      size: $(stat -c %s "$OUT/libslinky-engine.so") bytes"

# ---- the constant alignment gate, on the file that will actually ship ----
# Same check the Windows build runs: src/data/rdata.s is a sequential dump, so an insertion that is not a
# multiple of 16 bytes slides every later constant, and the 16-byte ones are read with aligned SSE loads,
# which fault in-world instead of misbehaving quietly.
if command -v python >/dev/null 2>&1; then PY=python; elif command -v python3 >/dev/null 2>&1; then PY=python3; else PY=""; fi
if [[ -z "$PY" ]]; then
    echo "[X] python is required to verify the constant alignment" >&2; exit 1
fi
"$PY" "$ENGINE/audit/AlignAudit.py" "$OUT/libslinky-engine.so" || {
    echo "[X] a 16-byte constant is read misaligned - pad the insertion in src/data/rdata.s to 16 bytes" >&2
    exit 1
}

if [[ $NATIVE -eq 0 ]]; then
    # Only the cross build can carry a path from the machine that is not the machine it ships to; a native build
    # has the player's own paths, which are nobody's problem but theirs.
    "$ROOT/tools/check-shipped-paths.sh" "$OUT/libslinky-engine.so"
fi

if [[ $DIST -eq 1 ]]; then
    mkdir -p "$ROOT/dist"
    cp -f "$OUT/libslinky-engine.so" "$ROOT/dist/libslinky-engine.so"
    echo "[4/4] dist/libslinky-engine.so"
else
    echo "[4/4] build/linux-engine/libslinky-engine.so (-Dist also copies it to dist/)"
fi
