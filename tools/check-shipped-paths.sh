#!/usr/bin/env bash
# slinkyFX - refuse to ship a file that names this build machine.
#
# Why this exists: everything here is cross-built. The Linux files are linked on Windows, by a Windows build of
# LLVM, from a shell that rewrites Unix-looking arguments into Windows paths before the tool sees them. That is
# invisible in a build log and fatal on a player's machine: in 1.6.0 the loader's ELF interpreter came out as
# "C:/Program Files/Git/lib64/ld-linux-x86-64.so.2", so the file existed, was executable, and would not start on
# any Linux system there is. A string scan of the finished artifact is the only check that catches that class
# without running it, and none of these files can be run here.
#
# So: no path from this machine in any shipped file. What "this machine" is spelled like depends on the file -
# a Linux file has no business holding a drive letter at all, while a Windows file holds those legitimately as
# part of its own format, so the two get different lists. The type comes out of the file, not out of a flag,
# because a gate that has to be told which half of the release it is looking at gets told the wrong thing
# eventually.
#
# Usage: tools/check-shipped-paths.sh <file> [<file>...]
set -euo pipefail
cd "$(dirname "$0")/.."
ROOT="$PWD"

if [[ $# -lt 1 ]]; then echo "usage: ${0##*/} <file>..." >&2; exit 2; fi

TOOLS=""
for cand in "$ROOT"/tools/llvm/clang+llvm-*/bin; do [[ -e "$cand/llvm-strings.exe" ]] && TOOLS="$cand" && break; done
[[ -n $TOOLS ]] || { echo "[X] no llvm-strings in tools/llvm - run tools/setup-llvm.sh" >&2; exit 1; }

# A literal turned into an ERE: a home folder is allowed to contain a dot, a plus, or a space.
esc() { sed 's/[][\\.(){}*+?|^$]/\\&/g' <<<"$1"; }

# The same directory in the three spellings a tool can record it in: the shell's, the Windows one with forward
# slashes (what an argument MSYS rewrote leaves behind), and the Windows one with backslashes.
PATTERNS=(
    "$(esc "$ROOT")" "$(esc "$(sed 's|^/\([a-z]\)/|\U\1:/|' <<<"$ROOT")")" "$(esc "$(sed 's|^/\([a-z]\)/|\U\1:\\|; s|/|\\|g' <<<"$ROOT")")"
    'Program Files'                        # the Git Bash install: where a rewritten /lib64 or /usr ends up
    'clang[+]llvm'                         # the bundled toolchain
    '\.sysroot'                            # the downloaded Linux sysroots
)
# Where a lone leading slash gets rewritten to on this shell. HOME and USERPROFILE are the same folder in the
# two spellings and both have to be on the list. TEMP and TMP stay off it: here they are /tmp, which is a folder
# every player has, and the strings our own code builds from it are the runtime ones we want to read.
for lit in "${HOME:-}" "${USERPROFILE:-}"; do
    [[ -n $lit ]] && PATTERNS+=("$(esc "${lit%/}")")
done
# Inside a Linux file a drive-qualified path can only be this machine's when it names one of the folders a
# Windows install keeps its programs, its users or its working trees in - which is exactly what an argument
# MSYS rewrote leaves behind. "Any drive letter followed by anything" would fire on the recovered code's own
# data blobs, where bytes that are not text at all happen to spell out M:/B.
LINUX_PATTERNS=('[A-Za-z]:[/\\](Users|Documents|Program Files|home|dev|tmp|Temp)')

pat_of() { local IFS='|'; echo "$*"; }
BASE_PAT=$(pat_of "${PATTERNS[@]}")
LINUX_PAT="$BASE_PAT$(pat_of "${LINUX_PATTERNS[@]}" | sed 's/^/|/')"

scan() { "$TOOLS/llvm-strings$EXT" "$1" | grep -E "$2" || true; }
EXT=.exe
[[ -e "$TOOLS/llvm-strings.exe" ]] || EXT=

# Prove the scan is live before trusting a clean answer from it: a gate that silently matches nothing says OK
# about every file forever, which is exactly how 1.6.0 got out. Every family gets its own probe, so a pattern
# that stopped matching is caught as "the check is broken" instead of "the build is clean".
PROBE=$(mktemp)
printf '%s\nC:/Program Files/Git/lib64/ld-linux-x86-64.so.2\n%s\\probe\n' \
    "$ROOT" "${USERPROFILE:-C:\\Users}" > "$PROBE"
[[ -n $(scan "$PROBE" "$BASE_PAT") ]] || { rm -f "$PROBE"
    echo "[X] the scan matched nothing, not even its own test string - the check is broken, not the build" >&2; exit 1; }
[[ -n $(scan "$PROBE" "$LINUX_PAT") ]] || { rm -f "$PROBE"
    echo "[X] the Linux scan does not match its own test string - the check is broken, not the build" >&2; exit 1; }
rm -f "$PROBE"

bad=0
for f in "$@"; do
    [[ -f $f ]] || { echo "[X] no such file: $f" >&2; exit 1; }

    if [[ $(head -c 4 "$f" | od -An -tx1 | tr -d ' \n') == "7f454c46" ]]; then
        kind=linux; pat="$LINUX_PAT"
    else
        kind=windows; pat="$BASE_PAT"
    fi

    # The header that decides whether a file can start at all, read out of the artifact rather than from the
    # link line that asked for it: an interpreter that is missing in a library is right, one that is a Windows
    # path in anything is a file that will never run, and one that is relative only the current folder agrees
    # with.
    if [[ $kind == linux ]]; then
        INTERP=$("$TOOLS/llvm-readelf$EXT" -l "$f" 2>/dev/null | sed -n 's/.*Requesting program interpreter: \(.*\)\]/\1/p')
        if [[ -n ${INTERP:-} && ${INTERP:0:1} != "/" ]]; then
            echo "[X] $(basename "$f") would not start: its interpreter is \"$INTERP\"" >&2
            bad=1; continue
        fi
        [[ -n ${INTERP:-} ]] && interp_note="starts through $INTERP" || interp_note="no interpreter (loaded by another program)"
    else
        interp_note="Portable Executable"
    fi

    hits=$(scan "$f" "$pat")
    if [[ -n $hits ]]; then
        bad=1
        echo "[X] $(basename "$f") names this build machine:" >&2
        sed 's/^/      /' <<<"$hits" | head -10 >&2
    else
        echo "      $(basename "$f") ($kind): no build-machine path, $interp_note"
    fi
done
[[ $bad -eq 0 ]] || { echo "    a path baked in at build time is a path a player cannot have - see the header." >&2; exit 1; }
