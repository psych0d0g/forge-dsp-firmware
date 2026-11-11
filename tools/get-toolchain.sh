#!/bin/sh
# avr-gcc 16.2.1 with the lower-subreg fix (tools/toolchain/), binutils and avr-libc of
# ZakKemble/avr-gcc-build v16.1.0-1; 64-bit double via -mdouble=64. Linux x86_64, pinned by sha256,
# from the release "toolchain-avr-gcc-16.2.1-p1" of this repository.
#   tools/get-toolchain.sh [DIR]      (default: $FORGE_TOOLCHAIN_DIR or ~/.cache/forge-dsp)
# Creates .toolchain/avr-gcc in the project as a link to the unpacked toolchain; platformio.ini
# uses it via platform_packages = toolchain-atmelavr@symlink://...
# Rebuild from source: tools/toolchain/build-avr-gcc.sh
set -eu
VER=16.2.1-p1
NAME=avr-gcc-$VER-x64-linux
FILE=$NAME.tar.xz
SHA256=909c8daa4a3bb2ac5b014b84887322afa1da551411b44baea245e383b6ef9273
URL=https://github.com/psych0d0g/forge-dsp-firmware/releases/download/toolchain-avr-gcc-$VER/$FILE

PROJECT=$(cd "$(dirname "$0")/.." && pwd)
DIR=${1:-${FORGE_TOOLCHAIN_DIR:-$HOME/.cache/forge-dsp}}
DEST=$DIR/$NAME
mkdir -p "$DIR"

if [ ! -x "$DEST/bin/avr-gcc" ]; then
	echo "downloading $URL"
	curl -sSL -o "$DIR/$FILE.part" "$URL"
	echo "$SHA256  $DIR/$FILE.part" | sha256sum -c -
	mv "$DIR/$FILE.part" "$DIR/$FILE"
	tar -xJf "$DIR/$FILE" -C "$DIR"
	rm -f "$DIR/$FILE"
fi

# PlatformIO package manifest
cat > "$DEST/package.json" <<JSON
{
  "name": "toolchain-atmelavr",
  "version": "3.160201.1",
  "description": "avr-gcc $VER (releases/gcc-16 + lower-subreg fix), -mdouble=64 capable",
  "system": ["linux_x86_64"]
}
JSON

mkdir -p "$PROJECT/.toolchain"
ln -sfn "$DEST" "$PROJECT/.toolchain/avr-gcc"
"$DEST/bin/avr-gcc" --version | head -1
