#!/bin/sh
set -eu

SCRIPT_DIR=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
REPO_ROOT=$(CDPATH= cd -- "$SCRIPT_DIR/.." && pwd)

SRC="$REPO_ROOT/payload/source"
TOOLS="$SCRIPT_DIR/toolchains"
BUILD="$SCRIPT_DIR/tmp"
OUT="$SCRIPT_DIR/output"

mkdir -p "$TOOLS" "$BUILD" "$OUT"

TOOLCHAIN_NAME=mips32--uclibc--stable-2017.05-toolchains-1-1
TOOLCHAIN_TARBALL="$TOOLS/$TOOLCHAIN_NAME.tar.bz2"
TOOLCHAIN_URL="https://toolchains.bootlin.com/downloads/releases/toolchains/mips32/tarballs/$TOOLCHAIN_NAME.tar.bz2"
TOOLCHAIN_DIR="$TOOLS/mips32--uclibc--stable"

if [ ! -x "$TOOLCHAIN_DIR/bin/mips-buildroot-linux-uclibc-gcc" ]; then
  if [ ! -f "$TOOLCHAIN_TARBALL" ]; then
    wget -O "$TOOLCHAIN_TARBALL" "$TOOLCHAIN_URL"
  fi
  tar -C "$TOOLS" -xf "$TOOLCHAIN_TARBALL"
fi

export PATH="$TOOLCHAIN_DIR/bin:$PATH"
CC=mips-buildroot-linux-uclibc-gcc
STRIP=mips-buildroot-linux-uclibc-strip

"$CC" -Os -static -s -o "$OUT/codex_dhcpd" "$SRC/codex_dhcpd.c"
python3 "$REPO_ROOT/tools/build_payload.py" --cc "$CC" --output "$OUT"

DROPBEAR_VERSION=2025.89
DROPBEAR_TARBALL="$BUILD/dropbear-$DROPBEAR_VERSION.tar.bz2"
DROPBEAR_URL="https://matt.ucc.asn.au/dropbear/releases/dropbear-$DROPBEAR_VERSION.tar.bz2"

if [ ! -f "$DROPBEAR_TARBALL" ]; then
  wget -O "$DROPBEAR_TARBALL" "$DROPBEAR_URL"
fi

rm -rf "$BUILD/dropbear-$DROPBEAR_VERSION"
tar -C "$BUILD" -xf "$DROPBEAR_TARBALL"
cd "$BUILD/dropbear-$DROPBEAR_VERSION"

./configure \
  --host=mips-buildroot-linux-uclibc \
  --disable-zlib \
  --disable-pam \
  --disable-lastlog \
  --disable-utmp \
  --disable-utmpx \
  --disable-wtmp \
  --disable-wtmpx \
  --disable-loginfunc \
  --disable-pututline \
  --disable-pututxline \
  CC="$CC" \
  CFLAGS="-Os -static" \
  LDFLAGS="-static"

make -j"$(nproc)" MULTI=1 PROGRAMS="dropbear dropbearkey dbclient scp" dropbearmulti
"$STRIP" dropbearmulti || true
cp dropbearmulti "$OUT/dropbearmulti"

cd "$OUT"
ln -sf dropbearmulti dropbear
ln -sf dropbearmulti dropbearkey
md5sum codex_dhcpd codex_portal codex_hbus codex_hal_ltcp codex_bthid_keyboard codex_webui dropbearmulti > MD5SUMS
file codex_dhcpd codex_portal codex_hbus codex_hal_ltcp codex_bthid_keyboard codex_webui dropbearmulti > FILES
ls -l codex_dhcpd codex_portal codex_hbus codex_hal_ltcp codex_bthid_keyboard codex_webui dropbearmulti > MANIFEST.txt
cat MD5SUMS >> MANIFEST.txt
