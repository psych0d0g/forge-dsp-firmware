#!/bin/sh
# avr-gcc 16.1.0 with avr-libc (64-bit double via -mdouble=64) for PlatformIO.
# Linux x86_64 build from github.com/ZakKemble/avr-gcc-build, pinned by sha256.
#   tools/get-toolchain.sh [DIR]      (default: $FORGE_TOOLCHAIN_DIR or ~/.cache/forge-dsp)
# Creates .toolchain/avr-gcc in the project as a link to the unpacked toolchain; platformio.ini
# uses it via platform_packages = toolchain-atmelavr@symlink://...
set -eu
VER=16.1.0
TAG=v16.1.0-1
FILE=avr-gcc-$VER-x64-linux.tar.bz2
SHA256=8621ecc6514df50202b58b23b0f8f72f0e535ec35ee40426194e9a15c57692f6
URL=https://github.com/ZakKemble/avr-gcc-build/releases/download/$TAG/$FILE

PROJECT=$(cd "$(dirname "$0")/.." && pwd)
DIR=${1:-${FORGE_TOOLCHAIN_DIR:-$HOME/.cache/forge-dsp}}
DEST=$DIR/avr-gcc-$VER-x64-linux
mkdir -p "$DIR"

if [ ! -x "$DEST/bin/avr-gcc" ]; then
	echo "downloading $URL"
	curl -sSL -o "$DIR/$FILE.part" "$URL"
	echo "$SHA256  $DIR/$FILE.part" | sha256sum -c -
	mv "$DIR/$FILE.part" "$DIR/$FILE"
	tar -xjf "$DIR/$FILE" -C "$DIR"
	rm -f "$DIR/$FILE"
fi

# PlatformIO package manifest
cat > "$DEST/package.json" <<JSON
{
  "name": "toolchain-atmelavr",
  "version": "3.160100.0",
  "description": "avr-gcc $VER (ZakKemble/avr-gcc-build $TAG), -mdouble=64 capable",
  "system": ["linux_x86_64"]
}
JSON

mkdir -p "$PROJECT/.toolchain"
ln -sfn "$DEST" "$PROJECT/.toolchain/avr-gcc"
"$DEST/bin/avr-gcc" --version | head -1
