#!/usr/bin/env bash
# slinkyFX - compile the Linux-only C++ for the target, from this machine.
#
# The loader (Slinky/src/linux) and the bootstrap (SlinkyNative/src, the five files that make
# libslinky-native.so) are not built by build.sh on Windows - on Linux, a player builds them with their own
# compiler. That leaves the code the players compile untested by anything here, which is the worst place to
# find out: a typo becomes "slinkyFX does not build on Arch".
#
# So this runs the same sources through the bundled clang with the Linux target and the Debian sysroot that
# tools/setup-linux-sysroot.sh lays out, with the warning set the engine build uses. The loader compiles twice,
# with and without X11: the README has players install libX11, and that branch is the one they get.
#
# It stops at the objects: the link belongs on the player's machine, next to their own libstdc++, and cross-linking
# here would record a C++ runtime newer than the one they have.
#
# Usage (from the repository root):
#   tools/check-linux-bootstrap.sh
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
SR="$ROOT/.sysroot/linux"
OUT="$ROOT/build/linux-check"

BIN=""
for cand in "$ROOT"/tools/llvm/clang+llvm-*/bin; do
    [[ -x "$cand/clang++" || -f "$cand/clang++.exe" ]] && BIN="$cand" && break
done
if [[ -z "$BIN" ]]; then
    echo "[X] no bundled LLVM in tools/llvm - run tools/setup-llvm.sh first" >&2
    exit 1
fi
CLANGXX="$BIN/clang++"
[[ -f "$CLANGXX" ]] || CLANGXX="$CLANGXX.exe"

if [[ ! -f "$SR/usr/include/x86_64-linux-gnu/c++/14/bits/c++config.h" ]]; then
    echo "[X] no C++ headers in .sysroot/linux - run tools/setup-linux-sysroot.sh first" >&2
    exit 1
fi

mkdir -p "$OUT"
# The driver looks for a GCC installation and finds none next to a Windows LLVM, so the standard library is
# named directly: Debian splits it into the portable tree and the machine-generated config half.
CXX=(-nostdinc++
     -isystem "$SR/usr/include/c++/14"
     -isystem "$SR/usr/include/x86_64-linux-gnu/c++/14"
     -isystem "$SR/usr/include/c++/14/backward"
     -isystem "$SR/usr/include"
     -isystem "$SR/usr/include/x86_64-linux-gnu")
WARN=(-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers)

echo "[1/3] The bootstrap that becomes libslinky-native.so..."
for f in SoMain Logger JvmManager MinecraftDetector PayloadLoader; do
    "$CLANGXX" --target=x86_64-unknown-linux-gnu --sysroot "$SR" "${CXX[@]}" \
        -std=c++17 -fPIC "${WARN[@]}" \
        -I"$ROOT/SlinkyNative/include" -I"$ROOT/SlinkyNative/include/jdk" \
        -I"$ROOT/SlinkyNative/include/jdk/linux" -I"$ROOT/SlinkyNative/include/w32" \
        -c -o "$OUT/bootstrap_$f.o" "$ROOT/SlinkyNative/src/$f.cpp"
    echo "      $f.cpp"
done

echo "[2/3] The loader (without X11: /proc detection alone must compile)..."
for f in ConsoleUI Injector Main ProcessUtil; do
    "$CLANGXX" --target=x86_64-unknown-linux-gnu --sysroot "$SR" "${CXX[@]}" \
        -std=c++17 "${WARN[@]}" \
        -I"$ROOT/Slinky/src/linux" \
        -c -o "$OUT/loader_$f.o" "$ROOT/Slinky/src/linux/$f.cpp"
    echo "      $f.cpp"
done

echo "[3/3] The loader with X11 (window-title matching - what a player who followed the README compiles)..."
"$CLANGXX" --target=x86_64-unknown-linux-gnu --sysroot "$SR" "${CXX[@]}" \
    -std=c++17 "${WARN[@]}" -DSLINKY_HAVE_X11 \
    -I"$ROOT/Slinky/src/linux" \
    -c -o "$OUT/loader_ProcessUtil_x11.o" "$ROOT/Slinky/src/linux/ProcessUtil.cpp"
echo "      ProcessUtil.cpp"

echo "[OK] 10 Linux-only objects: the bootstrap, the loader both ways (build/linux-check)"
