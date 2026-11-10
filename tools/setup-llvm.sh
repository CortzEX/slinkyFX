#!/usr/bin/env bash
# Download and extract the LLVM/clang-cl toolchain used to build the slinkyFX native engine.
# The archive is placed under tools/llvm/ inside the repository.
set -euo pipefail
cd "$(dirname "$0")/.."

LLVM_VERSION="23.1.4"
ARCHIVE="clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc.tar.xz"
URL="https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${ARCHIVE//+/%2B}"
DEST="tools/llvm"

if [[ -f "${DEST}/clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc/bin/clang-cl.exe" ]]; then
    echo "LLVM ${LLVM_VERSION} already present in ${DEST}"
    exit 0
fi

mkdir -p tools/llvm-download

echo "Downloading LLVM ${LLVM_VERSION} (~860 MB)..."
if command -v curl >/dev/null 2>&1; then
    curl -L -o "tools/llvm-download/${ARCHIVE}" "$URL"
elif command -v wget >/dev/null 2>&1; then
    wget -O "tools/llvm-download/${ARCHIVE}" "$URL"
else
    echo "Need curl or wget to download LLVM" >&2
    exit 1
fi

echo "Extracting..."
python3 -c "import tarfile; tarfile.open('tools/llvm-download/${ARCHIVE}', 'r:xz').extractall('${DEST}')"

echo "LLVM ${LLVM_VERSION} ready in ${DEST}/clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc/bin"
