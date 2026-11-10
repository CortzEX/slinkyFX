#!/usr/bin/env bash
# slinkyFX - lay out the Linux sysroots the two cross-builds work against.
#
#   .sysroot/linux        current Debian: glibc, libstdc++, kernel and X11 headers - what the compile checks use
#   .sysroot/glibc-2.31   Ubuntu 20.04's C runtime: what the engine links against
#   .sysroot/focal        Ubuntu 20.04's headers and C++ runtime: what the loader and the bootstrap are built with
#
# All three come out of the distribution archives as .deb files. A .deb is an ar container around a tar.xz, so
# opening one takes the bundled llvm-ar and tar - no distribution gets installed, nothing outside .sysroot/
# is touched, and the whole tree is re-creatable on any machine with network. .sysroot/ is a build-machine
# input like tools/llvm: it is not shipped with the client.
#
# The halves are deliberately different releases. What a shared object needs of libc is recorded inside it
# as versioned references (fmod@GLIBC_2.27), and the loader refuses to start an image whose newest reference is
# newer than the libc it lands on. Headers from the current release, link-time libraries from the oldest one
# worth supporting, and the linker itself proves the result: a symbol that the old libc does not have fails the
# link instead of failing in someone's game.
#
# The versions named below are the ones the engine was built and checked against. A mirror eventually moves a
# revision out of its pool, so every download falls back to the archive host, which keeps them.
#
# Usage (from the repository root, Git Bash on Windows or any Linux shell):
#   tools/setup-linux-sysroot.sh          fetch whatever is missing, then verify the layout
#   tools/setup-linux-sysroot.sh -Force   download again even if the tree is already there
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
SR="$ROOT/.sysroot/linux"
LEGACY="$ROOT/.sysroot/glibc-2.31"
FOCAL="$ROOT/.sysroot/focal"
CACHE="$ROOT/.sysroot/deb"

FORCE=0
for arg in "$@"; do
    case "$arg" in
        -Force|--force) FORCE=1 ;;
        *) echo "unknown argument: $arg" >&2; exit 2 ;;
    esac
done

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
AR=$(exe llvm-ar)
READELF=$(exe llvm-readelf)
for tool in tar curl; do
    command -v "$tool" >/dev/null 2>&1 || { echo "[X] $tool is required to unpack the archives" >&2; exit 1; }
done

# deb13u4 is the revision the engine was verified against; the archive hosts keep what the live pool drops.
HOSTS_DEBIAN=(https://deb.debian.org/debian https://archive.debian.org/debian)
HOSTS_UBUNTU=(https://archive.ubuntu.com/ubuntu http://old-releases.ubuntu.com/ubuntu)

have() { [[ -e $1 ]]; }

# The whole tree as one list, used both to decide whether anything has to be fetched and to report what is
# still absent. Two lists is how a half-extracted tree gets accepted: the fast check passes on the files it
# happens to name, the script exits "already in place", and the file the build actually needs is never fetched.
REQUIRED=(
    "$SR/usr/include/stdio.h"
    "$SR/usr/include/x86_64-linux-gnu/asm/errno.h"
    "$SR/usr/include/asm/errno.h"
    "$SR/usr/include/asm-generic/errno-base.h"
    "$SR/usr/include/linux/types.h"
    "$SR/usr/include/c++/14/vector"
    "$SR/usr/include/x86_64-linux-gnu/c++/14/bits/c++config.h"
    "$SR/usr/include/X11/Xlib.h"
    "$SR/usr/include/X11/Xatom.h"
    "$SR/usr/lib/x86_64-linux-gnu/crti.o"
    "$SR/usr/lib/x86_64-linux-gnu/crtn.o"
    "$SR/usr/lib/x86_64-linux-gnu/libc.so.6"
    "$LEGACY/lib/x86_64-linux-gnu/libc.so.6"
    "$LEGACY/lib/x86_64-linux-gnu/ld-linux-x86-64.so.2"
    "$FOCAL/usr/include/x86_64-linux-gnu/sys/stat.h"
    "$FOCAL/usr/include/c++/9/vector"
    "$FOCAL/usr/include/x86_64-linux-gnu/c++/9/bits/c++config.h"
    "$FOCAL/usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.28"
    "$FOCAL/lib/x86_64-linux-gnu/libgcc_s.so.1"
)
incomplete() {
    local f
    for f in "${REQUIRED[@]}"; do have "$f" || return 0; done
    return 1
}
if [[ $FORCE -eq 0 ]] && ! incomplete; then
    echo "Linux sysroot already in place (.sysroot/linux, .sysroot/glibc-2.31)"
    exit 0
fi

get() { # <name of the host array> <path inside pool/main>
    local -n hosts=$1
    local pool=$2
    local name=$(basename "$pool")
    local deb="$CACHE/$name"
    if [[ -f $deb && $FORCE -eq 0 ]]; then
        echo "       cached $name"
        return 0
    fi
    mkdir -p "$CACHE"
    local host
    for host in "${hosts[@]}"; do
        echo "       $host/pool/main/$pool"
        if curl -fL --retry 2 -sS -o "$deb" "$host/pool/main/$pool"; then return 0; fi
        rm -f "$deb"
    done
    echo "[X] could not download $name - neither host still carries it" >&2
    return 1
}

unpack() { # <path inside pool/main> <destination> <members to keep, as tar wildcards>
    local pool=$1
    local dest=$2
    shift 2
    local name=$(basename "$pool")
    local deb="$CACHE/$name"
    local work="$CACHE/${name%.deb}"
    rm -rf "$work"; mkdir -p "$work" "$dest"
    # llvm-ar writes the container's members into the current directory, so the unpack runs inside the scratch
    ( cd "$work" && "$AR" x "$deb" >/dev/null )
    # -C before the archive: after it, tar reads the destination as another member name and extracts nothing.
    # Only the listed members are taken - a full extract also carries /lib64 symlinks pointing at absolute
    # paths, which tar cannot create here and reports as a failure of the whole extraction.
    tar -C "$dest" -xf "$work/data.tar.xz" --wildcards "$@"
    rm -rf "$work"
    echo "       unpacked $name"
}

package() { # <name of the host array> <path in pool/main> <destination> <members to keep>
    local hosts=$1
    local pool=$2
    local dest=$3
    shift 3
    get "$hosts" "$pool"
    unpack "$pool" "$dest" "$@"
}

echo "[1/6] Debian 13 headers and start-up objects -> .sysroot/linux"
package HOSTS_DEBIAN g/glibc/libc6_2.41-12+deb13u4_amd64.deb "$SR" './usr/lib/x86_64-linux-gnu/*'
package HOSTS_DEBIAN g/glibc/libc6-dev_2.41-12+deb13u4_amd64.deb "$SR" './usr/include/*' './usr/lib/x86_64-linux-gnu/*'
# Debian keeps the kernel's own headers under /usr/lib/linux/uapi/<arch> and reaches them from the include path
# with a thousand symlinks - one directory per architecture. tar cannot create those here, and a failed link
# makes it report the whole extraction as failed, so the link dirs are excluded and the one this build reads is
# copied into place instead: <asm/...> and <x86_64-linux-gnu/asm/...>, the two spellings the package links to
# the same uapi/x86 tree.
package HOSTS_DEBIAN l/linux/linux-libc-dev_6.12.107-1_all.deb "$SR" \
    --exclude='*-linux-gnu*' --exclude='./usr/include/asm' \
    './usr/include/*' './usr/lib/linux/*'
for dir in asm x86_64-linux-gnu/asm; do
    rm -rf "$SR/usr/include/$dir"
    cp -r "$SR/usr/lib/linux/uapi/x86/asm" "$SR/usr/include/$dir"
done

echo "[2/6] Debian 13 C++ standard library headers -> .sysroot/linux"
# The engine itself is C with no standard library, so nothing above carries <string> or <vector>. The bootstrap
# does, and tools/check-linux-bootstrap.sh compiles it here: the same sources the players' own g++ builds on
# their machine, run through a compiler before anyone has to. Headers only - a player who builds from source
# links step 5's files against their own libstdc++, and Debian splits the tree: <prefix>/include/c++/14 is the
# portable half, <prefix>/include/<triple>/c++/14 the machine-generated config (bits/c++config.h). Both are
# taken. The release files are compiled against step 5's older headers instead, for the reason given there.
package HOSTS_DEBIAN g/gcc-14/libstdc++-14-dev_14.2.0-19_amd64.deb "$SR" \
    './usr/include/c++/14/*' './usr/include/x86_64-linux-gnu/c++/14/*'

echo "[3/6] X11 headers -> .sysroot/linux"
# Not for the engine, which never links a window system: the loader has an optional X11 branch (match the game
# by window title, which is what makes XWayland sessions work as well as X11 ones) and it is the branch most
# players actually compile, because the README tells them to install libx11-dev. Headers only - the check stops
# at the objects, and libX11 belongs to the machine that links.
package HOSTS_DEBIAN libx/libx11/libx11-dev_1.8.12-1_amd64.deb "$SR" './usr/include/X11/*'
# ... and the protocol half of <X11> (X.h, Xatom.h, Xmd.h), which Debian ships from the xorgproto source
# package rather than next to Xlib.h. Xatom.h is the one ProcessUtil.cpp reads the window properties with.
package HOSTS_DEBIAN x/xorgproto/x11proto-dev_2024.1-1_all.deb "$SR" './usr/include/X11/*'

echo "[4/6] Ubuntu 20.04 C runtime -> .sysroot/glibc-2.31"
# only the runtime libraries: the headers in .sysroot/linux are the ones the sources see
package HOSTS_UBUNTU g/glibc/libc6_2.31-0ubuntu9.18_amd64.deb "$LEGACY" './lib/x86_64-linux-gnu/*'

echo "[5/6] Ubuntu 20.04 headers and C++ runtime -> .sysroot/focal"
# tools/build-linux-binaries.sh compiles the loader and the bootstrap against these, which is what keeps the
# release files off the newest libc: glibc renamed the scan and convert functions (sscanf becomes
# __isoc23_sscanf) in 2.36, and a reference bought from current headers is a reference a five-year-old
# distribution cannot answer. Headers from 2020 promise less, so the files ask for less.
#
# The libstdc++ and libgcc the two files name at run time come from here too. Both are linked as shared
# libraries, not copied in: the player's own libstdc++ answers the name, and it is always newer than the 2020
# one, which is the direction C++ runtimes do go in. What has to stay low is the *recorded* requirement -
# GLIBCXX_3.4.26, GCC 9 - so a prebuilt file loads on anything since, including Ubuntu 22.04's and Debian 12's.
package HOSTS_UBUNTU g/glibc/libc6-dev_2.31-0ubuntu9.18_amd64.deb "$FOCAL" \
    './usr/include/*' './usr/lib/x86_64-linux-gnu/*' '--exclude=./usr/lib/x86_64-linux-gnu/*.so'
# libc6-dev's headers reach the kernel's UAPI through linux-libc-dev, and the 20.04 pair has to stay together:
# glibc 2.31's bits struct against the 5.4 kernel headers, the combination Ubuntu 20.04 shipped and tested.
package HOSTS_UBUNTU l/linux/linux-libc-dev_5.4.0-216.236_amd64.deb "$FOCAL" './usr/include/*'
package HOSTS_UBUNTU g/gcc-9/libstdc++-9-dev_9.4.0-1ubuntu1~20.04.2_amd64.deb "$FOCAL" \
    './usr/include/c++/9/*' './usr/include/x86_64-linux-gnu/c++/9/*'
# ... and the two runtime libraries themselves, by their real file names. Debian and Ubuntu both ship the
# short name as a symlink into an absolute path that does not exist here, so tar is told to take the versioned
# file and leave the link alone - the build passes -l:libstdc++.so.6.0.28 for exactly that reason.
package HOSTS_UBUNTU g/gcc-10/libstdc++6_10.5.0-1ubuntu1~20.04_amd64.deb "$FOCAL" \
    './usr/lib/x86_64-linux-gnu/libstdc++.so.6.0.28'
package HOSTS_UBUNTU g/gcc-10/libgcc-s1_10.5.0-1ubuntu1~20.04_amd64.deb "$FOCAL" \
    './lib/x86_64-linux-gnu/libgcc_s.so.1'

echo "[6/6] Verifying"
missing=""
for f in "${REQUIRED[@]}"; do
    have "$f" || missing="$missing ${f#"$ROOT"/}"
done
if [[ -n $missing ]]; then
    echo "[X] still missing:$missing" >&2
    exit 1
fi

newest() { "$READELF" -V "$1" | grep -oE 'GLIBC_[0-9.]+' | sort -u -t_ -k2 -V | tail -1; }
echo "      Debian 13 headers see up to   $(newest "$SR/usr/lib/x86_64-linux-gnu/libc.so.6")"
echo "      the 20.04 C runtime answers   $(newest "$LEGACY/lib/x86_64-linux-gnu/libc.so.6")"
echo "      -> tools/build-linux-engine.sh (the engine), tools/check-linux-bootstrap.sh (the compile check)"
echo "         tools/build-linux-binaries.sh (the two release files, built against .sysroot/focal)"
