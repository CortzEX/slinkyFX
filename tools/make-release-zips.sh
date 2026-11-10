#!/usr/bin/env bash
# slinkyFX - pack and verify the two release archives.
#
# One command, because packing by hand is where the near-misses happen. A release is five files and two zips:
#   dist/slinky-loader.exe  dist/slinky-native.dll                       the Windows pair
#   build/linux-binaries/slinky-loader  .../libslinky-native.so          the Linux loader and bootstrap
#   build/linux-engine/libslinky-engine.so                               the engine both versions run
#
# The Linux loader is copied from build/, never from dist/, and that is not tidiness: on this filesystem a file
# created in dist/ without an extension lands on the same-stem Windows one, so an extensionless "slinky-loader"
# sitting in dist/ overwrote slinky-loader.exe and a shipped zip went out with a Linux ELF named .exe.
#
# Everything is read back out of the finished archive, because a zip written by a script that meant well is not
# the same thing as a zip whose entries were opened again and looked at: the file type, the permission bits, and
# the record that tells unzip those bits are real (an archive written as DOS-carriage loses the executable flag,
# and the player's first command then fails with "Permission denied").
#
# Usage (repository root, Git Bash): tools/make-release-zips.sh 2.0.0
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"
V=${1:-}
[[ $V =~ ^[0-9]+\.[0-9]+\.[0-9]+$ ]] || { echo "usage: ${0##*/} <version, e.g. 1.6.1>" >&2; exit 2; }
shift
[[ $# -eq 0 ]] || { echo "unknown argument: $*" >&2; exit 2; }

WIN=(dist/slinky-loader.exe dist/slinky-native.dll)
LINUX=(build/linux-binaries/slinky-loader build/linux-binaries/libslinky-native.so
       build/linux-engine/libslinky-engine.so)
for f in "${WIN[@]}" "${LINUX[@]}"; do
    [[ -f $f ]] || { echo "[X] $f is not there - build it first (./build.sh, tools/build-linux-binaries.sh," \
        "tools/build-linux-engine.sh)" >&2; exit 1; }
done

BIN=$(ls -d "$ROOT"/tools/llvm/clang+llvm-*/bin 2>/dev/null | head -1)
[[ -n ${BIN:-} && -e "$BIN/llvm-readelf.exe" ]] || { echo "[X] no bundled LLVM - run tools/setup-llvm.sh" >&2; exit 1; }

PY=""
for cand in python python3; do command -v "$cand" >/dev/null 2>&1 && PY=$cand && break; done
[[ -n $PY ]] || { echo "[X] python is required to write and verify the archives" >&2; exit 1; }

echo "[1/3] The five shipped files, checked before they go anywhere near an archive..."
# The header that stops a file starting at all, read out of the artifact and not from the link line that asked
# for it - see the note in tools/build-linux-binaries.sh.
INTERP=$("$BIN/llvm-readelf.exe" -l "${LINUX[0]}" | sed -n 's/.*Requesting program interpreter: \(.*\)\]/\1/p')
[[ $INTERP == /lib64/ld-linux-x86-64.so.2 ]] || {
    echo "[X] slinky-loader starts through \"$INTERP\" - it would not start on any Linux system" >&2; exit 1; }
echo "      loader interpreter: $INTERP"
# Both halves of the release, not just the cross-built one: a Windows binary carries the folder that linked it
# when the linker asks for a program database, and that folder is this machine, not the player's.
"$ROOT/tools/check-shipped-paths.sh" "${WIN[@]}" "${LINUX[@]}"

echo "[2/3] Writing dist/slinkyFX-$V-windows.zip and dist/slinkyFX-$V-linux.zip..."
"$PY" - "$V" "${WIN[@]}" "${LINUX[@]}" <<'PY'
import hashlib, os, sys, zipfile
v, files = sys.argv[1], sys.argv[2:]
win, linux = files[:2], files[2:]

def pack(path, pairs):
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        for src, name in pairs:
            info = zipfile.ZipInfo(name, (2000, 1, 1, 0, 0, 0))   # fixed stamp: two builds of the same
            info.compress_type = zipfile.ZIP_DEFLATED             # sources give the same archive
            info.create_system = 3                                # 3 = Unix; without it unzip on a player's
            info.external_attr = (0o755 << 16) | 0x10             # box ignores the mode and drops +x
            with open(src, "rb") as f:
                z.writestr(info, f.read())

pack("dist/slinkyFX-%s-windows.zip" % v, [(p, os.path.basename(p)) for p in win])
pack("dist/slinkyFX-%s-linux.zip" % v, [(p, os.path.basename(p)) for p in linux])

hashes, ok = [], True
for z in ["dist/slinkyFX-%s-windows.zip" % v, "dist/slinkyFX-%s-linux.zip" % v]:
    arc = zipfile.ZipFile(z)
    assert arc.testzip() is None, "%s is corrupt" % z
    print("      %s" % os.path.basename(z))
    for i in arc.infolist():
        body = arc.read(i.filename)
        elf = body.startswith(b"\x7fELF")
        want_elf = i.filename.endswith(".so") or i.filename == "slinky-loader"
        good = elf == want_elf and i.external_attr >> 16 == 0o755 and i.create_system == 3
        ok = ok and good
        print("        %-22s %9d bytes  %s  mode %04o  written as %s%s" % (
            i.filename, i.file_size, "ELF " if elf else "PE  ",
            i.external_attr >> 16, "Unix" if i.create_system == 3 else "DOS",
            "" if good else "   <== WRONG"))
        hashes.append((i.filename, hashlib.sha256(body).hexdigest().upper()))
    hashes.append((os.path.basename(z), hashlib.sha256(open(z, "rb").read()).hexdigest().upper()))
if not ok:
    sys.exit("[X] an entry is not the file type its name says, or will not unpack executable")
print("[3/3] sha256")
for name, digest in hashes:
    print("      %-30s %s" % (name, digest))
PY
