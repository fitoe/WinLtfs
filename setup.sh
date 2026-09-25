#!/bin/bash
# winltfs one-time setup. Run inside an MSYS2 MINGW64 shell:
#   ./setup.sh
#
# Installs build dependencies, stages WinFsp developer files into build/wfsp
# (a path without spaces, which autotools needs), and runs autoreconf +
# configure. The WinFsp port patches are already committed on this branch, so
# there is nothing to patch here.
set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
SRC="$ROOT/ltfs"

if [ "${MSYSTEM:-}" != "MINGW64" ]; then
    echo "error: run this from an MSYS2 MINGW64 shell (not MSYS/UCRT64/cmd)" >&2
    exit 1
fi

if [ ! -f "$SRC/configure.ac" ]; then
    echo "error: $SRC/configure.ac not found - check out the 'winfsp' branch" >&2
    exit 1
fi

echo "==> Installing build dependencies (pacman)"
pacman -S --needed --noconfirm \
    git autoconf automake libtool make pkgconf \
    mingw-w64-x86_64-toolchain mingw-w64-x86_64-libxml2 \
    mingw-w64-x86_64-icu mingw-w64-x86_64-pkgconf

echo "==> Staging WinFsp developer files into build/wfsp"
WINFSP="${WINFSP:-/c/Program Files (x86)/WinFsp}"
if [ ! -f "$WINFSP/lib/winfsp-x64.lib" ]; then
    echo "error: WinFsp developer files not found at: $WINFSP" >&2
    echo "Install WinFsp (https://winfsp.dev) and select the 'Developer' feature," >&2
    echo "or point the WINFSP environment variable at the install directory." >&2
    exit 1
fi
mkdir -p "$ROOT/build/wfsp/lib" "$ROOT/build/wfsp/bin"
cp -r "$WINFSP/inc" "$ROOT/build/wfsp/"
# MSVC import library; GNU ld links it fine under a MinGW-style name
cp "$WINFSP/lib/winfsp-x64.lib" "$ROOT/build/wfsp/lib/libwinfsp-x64.a"
cp "$WINFSP/bin/winfsp-x64.dll" "$ROOT/build/wfsp/bin/"

echo "==> autoreconf (regenerating 2012-era autotools files)"
cd "$SRC"
# Filter autoreconf's noise for display, but abort on a real failure: the pipe
# would otherwise return grep's status (and grep exits 1 when it filters out
# every line), hiding an autoreconf error - which once masked missing autotools
# regeneration. PIPESTATUS[0] is autoreconf's own exit code.
set +e
autoreconf -fi 2>&1 | grep -v 'warning\|obsolete\|expanded from\|the top level\|^\.\./\|^aclocal\|^configure\.ac'
ac_status=${PIPESTATUS[0]}
set -e
if [ "$ac_status" -ne 0 ]; then
    echo "error: autoreconf -fi failed (exit $ac_status) - see output above" >&2
    exit 1
fi

echo "==> configure"
# -D_FILE_OFFSET_BITS=64 MUST be a command-line define so it is set before any
# system header in every TU: modern MinGW-w64 then types off_t as off64_t
# (64-bit). Without it off_t is a 32-bit long on Win64 and all file I/O caps at
# 2 GiB (offset 0x80000000 wraps negative). win_util.h's late "#define
# _FILE_OFFSET_BITS 64" lands after <sys/types.h> and is too late to matter.
#
# HPE StoreOpen 3.4.2 renamed the Windows build macro HP_mingw_BUILD ->
# HPE_mingw_BUILD; we define BOTH so the upstream 3.4.2 code (HPE_) and our
# WinFsp port's guards (HP_) are all active.
./configure --host=x86_64-w64-mingw32 --build=x86_64-w64-mingw32 \
    --with-winfsp="$ROOT/build/wfsp" \
    CFLAGS="-Dmingw_PLATFORM=1 -DHP_mingw_BUILD=1 -DHPE_mingw_BUILD=1 -D_FILE_OFFSET_BITS=64 -DWINVER=0x0601 -D_WIN32_WINNT=0x0601"

echo
echo "Setup complete. Build with: ./build.sh"
