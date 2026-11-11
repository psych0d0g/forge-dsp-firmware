#!/bin/bash
# Patched avr-gcc: GCC releases/gcc-16 at a pinned commit + patches/, binutils and avr-libc
# taken from the avr-gcc 16.1.0 build of tools/get-toolchain.sh (same configuration, same
# multilib layout, -mdouble=64 -> avrxmega6/double64).
#   tools/toolchain/build-avr-gcc.sh <base toolchain dir> <output dir>
# Needs network (gcc.gnu.org git, GMP/MPFR/MPC via download_prerequisites), ~15 min on 56 threads.
set -euo pipefail
GCC_REV=2cc1be9c9bf78ba01ee37fa09d9906060685cb56   # releases/gcc-16, 2026-11-11 (16.2.1)
here=$(cd "$(dirname "$0")" && pwd)
base=$(realpath "$1"); out=$(realpath -m "$2")
work=${WORK:-/tmp/avr-gcc-build}
mkdir -p "$work" && cd "$work"
if [ ! -d gcc ]; then
	git init -q gcc && git -C gcc remote add origin https://gcc.gnu.org/git/gcc.git
	git -C gcc fetch -q --depth 1 origin "$GCC_REV" && git -C gcc checkout -q FETCH_HEAD
	(cd gcc && ./contrib/download_prerequisites)
	for p in "$here"/patches/*.patch; do git -C gcc apply "$p"; done
fi
rm -rf "$out" && cp -a "$base" "$out"
export PATH="$out/bin:$PATH"
rm -rf build && mkdir build && cd build
../gcc/configure --prefix="$out" --build=x86_64-pc-linux-gnu --target=avr --enable-languages=c,c++ \
	--disable-nls --disable-libssp --disable-libada --with-dwarf2 --disable-shared --enable-static \
	--enable-plugin --with-gnu-as --with-gnu-ld --without-zstd
make -j"$(nproc)" all-gcc all-target-libgcc
make install-gcc install-target-libgcc
# the multilib layout must match the avr-libc of the base toolchain
test "$("$out/bin/avr-gcc" -mmcu=atxmega256a3u -mdouble=64 -print-multi-directory)" = "avrxmega6/double64"
"$out/bin/avr-gcc" -mmcu=atxmega256a3u -Os -S "$here/tests/ice-lower-subreg.c" -o /dev/null
"$out/bin/avr-gcc" --version | head -1
