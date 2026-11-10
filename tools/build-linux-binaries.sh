#!/usr/bin/env bash
# slinkyFX - build the two Linux files that go in the release zip: the loader and the bootstrap.
#
#   build/linux-binaries/slinky-loader         the player runs this
#   build/linux-binaries/libslinky-native.so   the bootstrap it injects, which starts libslinky-engine.so
#
# libslinky-engine.so - the client itself - is tools/build-linux-engine.sh's job. Those two scripts together
# make the three files a Linux release carries.
#
# Running ./build.sh on a Linux machine builds the same two programs with CMake and that machine's own
# compilers, and that stays the way to get files for the box you are on. This script exists because a release
# has to be one set of files for every distribution, which cannot be done by the compiler of the day: it is a
# Windows cross-build, so a mistake shows up here rather than in someone's game.
#
# Two sysroots, and they are used for different things:
#
#   .sysroot/focal   Ubuntu 20.04's headers and C++ runtime. The sources are compiled against these, and the
#                    libstdc++/libgcc the files ask for at run time come from here too.
#   .sysroot/linux   Debian 13's kernel headers and C runtime, used for the link.
#
# The compile uses the old headers because they are the ones that promise less. glibc renames the string and
# scan functions when it sees C++17-era code (sscanf becomes __isoc23_sscanf), and that reference is only
# answerable on glibc 2.36 and up - a current Debian or Fedora, but not much older. Headers from 2020 keep the
# plain names, so the files ask for far less than a build on today's machine would.
#
# The link takes the C runtime from Debian 13 because the loader calls stat(), and glibc only started
# exporting that name in 2.33. The version of every reference is whatever the linked library recorded for the
# symbol it answered, so this changes nothing else: the loader lands at GLIBC_2.34 and the bootstrap, which
# calls nothing new, stays at GLIBC_2.17. The numbers are checked below, not assumed.
#
# What that buys, in player terms: the loader and the bootstrap run on Debian 12/13, Ubuntu 22.04/24.04,
# any current Fedora and Arch. They do not run on Ubuntu 20.04 or Debian 11, both of which are out of
# support; on those, build from source. The engine keeps its own, lower GLIBC_2.29 floor.
#
# The loader is built without the X11 branch, because libX11 is not in either sysroot. Window-title matching
# is the one thing that leaves out of a prebuilt loader: game detection still works, through /proc, which is
# how the loader finds the game on Wayland too. A player who wants the X11 branch builds from source with
# libx11-dev installed, and the README says so.
#
# Usage (from the repository root, Git Bash on Windows):
#   tools/build-linux-binaries.sh            build both files
#   tools/build-linux-binaries.sh -Clean     compile again from nothing
#
# There is no -Dist here: the loader has no extension, and on this filesystem the shell creates a same-stem
# .exe instead, which would put a Linux binary in dist/ under a Windows name. The release zip is packed
# straight out of build/linux-binaries.
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
ENGINE="$ROOT/SlinkyNative"
LOADER="$ROOT/Slinky"
SR="$ROOT/.sysroot/focal"
NEW="$ROOT/.sysroot/linux"
LEG="$ROOT/.sysroot/glibc-2.31/lib/x86_64-linux-gnu"
OUT="$ROOT/build/linux-binaries"

CLEAN=0
for arg in "$@"; do
    case "$arg" in
        -Clean|--clean) CLEAN=1 ;;
        -Dist|--dist)   echo "[X] -Dist is gone: see the usage note above, pack the zip from build/linux-binaries" >&2; exit 2 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

if [[ "$(uname -s)" == "Linux" ]]; then
    echo "[X] this is the Windows cross-build. On Linux, run ./build.sh - it uses your own compilers," >&2
    echo "    and the files it makes are yours alone." >&2
    exit 1
fi

BIN=""
for cand in "$ROOT"/tools/llvm/clang+llvm-*/bin; do
    [[ -x "$cand/clang++" || -f "$cand/clang++.exe" ]] && BIN="$cand" && break
done
if [[ -z "$BIN" ]]; then
    echo "[X] no bundled LLVM in tools/llvm - run tools/setup-llvm.sh first" >&2
    exit 1
fi
exe() {
    if [[ -f "$BIN/$1.exe" ]]; then printf '%s/%s.exe' "$BIN" "$1"; else printf '%s/%s' "$BIN" "$1"; fi
}
CLANGXX=$(exe clang++); LLD=$(exe ld.lld); READELF=$(exe llvm-readelf)

for d in "$SR/usr/include/c++/9" \
         "$SR/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.28" "$SR/lib/x86_64-linux-gnu/libgcc_s.so.1" \
         "$NEW/usr/include/linux/types.h" "$NEW/usr/lib/x86_64-linux-gnu/Scrt1.o"; do
    if [[ ! -e $d ]]; then
        echo "[X] $d is missing - run tools/setup-linux-sysroot.sh first" >&2
        exit 1
    fi
done

[[ $CLEAN -eq 1 && -d $OUT ]] && rm -rf "$OUT"
mkdir -p "$OUT"

# The warning set is check-linux-bootstrap.sh's, so a source that silences a mistake here would have shown it
# there as well. Both scripts compile the same files; this one also links them.
CXXFLAGS="--target=x86_64-unknown-linux-gnu --sysroot $SR
-isystem $SR/usr/include/c++/9
-isystem $SR/usr/include/x86_64-linux-gnu/c++/9
-isystem $SR/usr/include/x86_64-linux-gnu
-isystem $SR/usr/include
-std=c++17 -fPIC -D_GNU_SOURCE=1
-ffile-prefix-map=$ROOT/=
-Wall -Wextra -Wno-unused-parameter -Wno-missing-field-initializers"

echo "[1/4] Compiling the bootstrap and the loader against Ubuntu 20.04 headers..."
for f in SoMain Logger JvmManager MinecraftDetector PayloadLoader; do
    # shellcheck disable=SC2086
    "$CLANGXX" $CXXFLAGS -I"$ENGINE/include" -I"$ENGINE/include/jdk" -I"$ENGINE/include/jdk/linux" \
        -I"$ENGINE/include/w32" -c "$ENGINE/src/$f.cpp" -o "$OUT/bootstrap_$f.o"
    echo "      $f.cpp"
done
for f in ConsoleUI Injector Main ProcessUtil; do
    # shellcheck disable=SC2086
    "$CLANGXX" $CXXFLAGS -I"$LOADER/src/linux" -c "$LOADER/src/linux/$f.cpp" -o "$OUT/loader_$f.o"
    echo "      $f.cpp"
done

# crtbeginS.o and friends are what the compiler driver would add; there is no Linux GCC installation beside the
# bundled LLVM, so the start-up objects are named by hand, as tools/build-linux-engine.sh does for the engine.
#
# The two files are linked against different C runtimes, and for one reason: the loader calls stat(), which
# glibc only began exporting in 2.33. The bootstrap calls nothing that new, so it links against the 20.04
# runtime and keeps the floor it measured here - GLIBC_2.17, older than the engine's own requirement.
STDLIBS=(-l:libstdc++.so.6.0.28 -l:libgcc_s.so.1)
BOOT_LIBS=("${STDLIBS[@]}" -l:libc.so.6 -l:libm.so.6 -l:libpthread.so.0 -l:libdl.so.2 -l:librt.so.1
           -l:ld-linux-x86-64.so.2)
LOAD_LIBS=("${STDLIBS[@]}" -l:libc.so.6 -l:libm.so.6 -l:ld-linux-x86-64.so.2)
BOOT_PATHS=(-L"$LEG" -L"$SR/usr/lib/x86_64-linux-gnu" -L"$SR/lib/x86_64-linux-gnu")
LOAD_PATHS=(-L"$NEW/usr/lib/x86_64-linux-gnu" -L"$SR/usr/lib/x86_64-linux-gnu" -L"$SR/lib/x86_64-linux-gnu")

echo "[2/4] Linking libslinky-native.so..."
"$LLD" -shared --sysroot="$SR" "${BOOT_PATHS[@]}" --soname=libslinky-native.so \
    --no-undefined -z now --build-id=none \
    -o "$OUT/libslinky-native.so" \
    "$SR/usr/lib/x86_64-linux-gnu/crti.o" \
    "$OUT"/bootstrap_*.o "${BOOT_LIBS[@]}" \
    "$SR/usr/lib/x86_64-linux-gnu/crtn.o"

echo "[3/4] Linking slinky-loader..."
# The interpreter goes in through a response file, and only works there. Written on the command line, Git Bash
# sees an argument that looks like a Unix path and rewrites it to a Windows one before ld.lld gets it, and lld
# records what it was given: v1.6.0 shipped "C:/Program Files/Git/lib64/ld-linux-x86-64.so.2", which no Linux
# system has, so the loader could not start anywhere. A file the linker reads itself never passes through the
# shell, so the string survives; MSYS2_ARG_CONV_EXCL and MSYS_NO_PATHCONV were both tried and both either
# changed nothing or broke the paths this script legitimately needs in Windows form. The gate further down
# reads the value back out of the finished file, because the argument is not the evidence - the artifact is.
printf '%s\n' '--dynamic-linker=/lib64/ld-linux-x86-64.so.2' > "$OUT/interp.rsp"
"$LLD" -pie --sysroot="$NEW" "${LOAD_PATHS[@]}" "@$OUT/interp.rsp" \
    --no-undefined -z now --build-id=none \
    -o "$OUT/slinky-loader" \
    "$NEW/usr/lib/x86_64-linux-gnu/Scrt1.o" "$NEW/usr/lib/x86_64-linux-gnu/crti.o" \
    "$OUT"/loader_*.o "${LOAD_LIBS[@]}" \
    "$NEW/usr/lib/x86_64-linux-gnu/crtn.o"

# ---- what the two files are allowed to ask of the machine they land on ----
# The floors are the point of this script, so they are read back out of the files rather than trusted to the
# reasoning above. A new libc reference pushes a floor up and stops the build here, instead of turning into
# "undefined symbol" on a player's older distribution.
floors() { # <file> -> "<glibc> <glibcxx>"
    local v
    v=$("$READELF" -V "$1" | grep -oE 'GLIBC_[0-9.]+|GLIBCXX_[0-9.]+|CXXABI_[0-9.]+' | sort -u -t_ -k2 -V)
    printf '%s %s' \
        "$(grep '^GLIBC_' <<<"$v" | tail -1)" \
        "$(grep -E '^GLIBCXX_|^CXXABI_' <<<"$v" | sed 's/^CXXABI_/GLIBCXX_/' | sort -u -t_ -k2 -V | tail -1)"
}
fail=()
read -r BOOT_G LIB_BOOT <<<"$(floors "$OUT/libslinky-native.so")"
read -r LOAD_G LIB_LOAD <<<"$(floors "$OUT/slinky-loader")"
echo "      libslinky-native.so: needs $BOOT_G / $LIB_BOOT   ($(( $(stat -c %s "$OUT/libslinky-native.so") / 1024 )) KiB)"
echo "      slinky-loader:       needs $LOAD_G / $LIB_LOAD   ($(( $(stat -c %s "$OUT/slinky-loader") / 1024 )) KiB)"

# Every library the two files name has to be one a distribution's base install already carries: nothing here
# may pull in X11, GL or a package the player is not told to install.
ALLOWED='^(libstdc\+\+\.so\.6|libgcc_s\.so\.1|libc\.so\.6|libm\.so\.6|libpthread\.so\.0|libdl\.so\.2|librt\.so\.1|ld-linux-x86-64\.so\.2)$'
for f in libslinky-native.so slinky-loader; do
    # || true: with pipefail, a grep that finds nothing to report is a failing pipeline, and "nothing outside
    # the allowed list" is the good answer here.
    extra=$(("$READELF" -d "$OUT/$f" | grep NEEDED | sed 's/.*\[//; s/\].*//' \
             | grep -vE "$ALLOWED" | tr '\n' ' ') || true)
    [[ -z $extra ]] || fail+=("$f names a library outside a base install: $extra")
done

for trio in "libslinky-native.so $BOOT_G GLIBC_2.17" "libslinky-native.so $LIB_BOOT GLIBCXX_3.4.26" \
            "slinky-loader $LOAD_G GLIBC_2.34" "slinky-loader $LIB_LOAD GLIBCXX_3.4.26"; do
    read -r file got want <<<"$trio"
    newest=$(printf '%s\n%s\n' "${got#*_}" "${want#*_}" | sort -V | tail -1)
    [[ "${got#*_}" == "$newest" ]] || fail+=("$file needs $got, above the $want this release allows")
done

# The bootstrap enters the game through a constructor, not through JNI_OnLoad: dlopen alone has to start it.
if ! "$READELF" -S "$OUT/libslinky-native.so" | grep -q "\.init_array"; then
    fail+=("libslinky-native.so has no .init_array, so the constructor would never run")
fi

# And the interpreter, read back out of the file rather than trusted to the argument that asked for it: it is
# the one header a program cannot be started without, and this script got it wrong in 1.6.0.
INTERP=$("$READELF" -l "$OUT/slinky-loader" | sed -n 's/.*Requesting program interpreter: \(.*\)\]/\1/p')
echo "      slinky-loader:       starts through ${INTERP:-nothing}"
[[ $INTERP == /lib64/ld-linux-x86-64.so.2 ]] ||
    fail+=("slinky-loader starts through \"$INTERP\"; the dynamic linker has to be /lib64/ld-linux-x86-64.so.2")

if [[ ${#fail[@]} -gt 0 ]]; then
    echo "[X] the two files ask for more than this release means to allow:" >&2
    for reason in "${fail[@]}"; do echo "    $reason" >&2; done
    exit 1
fi

# No path from this machine in either file - the same class of mistake as the interpreter above, and the same
# reason: nothing here can run a Linux binary to find out.
"$ROOT/tools/check-shipped-paths.sh" "$OUT/slinky-loader" "$OUT/libslinky-native.so"

echo "[4/4] build/linux-binaries/slinky-loader + build/linux-binaries/libslinky-native.so, checked"
echo "      the engine is build/linux-engine/libslinky-engine.so (tools/build-linux-engine.sh -Dist)"
