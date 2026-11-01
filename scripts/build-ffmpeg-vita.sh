#!/usr/bin/env bash
#
# Builds a minimal static FFmpeg for the PS Vita and installs it into a prefix.
#
# Why: the game's .rmv movies are Ogg/Theora/Vorbis. The "ffmpeg" package from
# vitasdk/vdpm is built with --disable-everything and has no Theora (VP3)
# decoder, so every movie failed to load. This is the same recipe as the vdpm
# package (same FFmpeg version, same flags) with the Theora/VP3 decoders added,
# and without the encoders/muxers/libmp3lame the game never uses.
#
# Usage:  scripts/build-ffmpeg-vita.sh [prefix]      (default: ./ffmpeg-vita)
# Then:   cmake ... -DSRR2_FFMPEG_PREFIX=<prefix>
#
set -euo pipefail

: "${VITASDK:?VITASDK must point to the vitasdk installation}"

FFMPEG_VER="${FFMPEG_VER:-9.0.2}"
FFMPEG_SHA256="${FFMPEG_SHA256:-8c3850283eb25fa026482078a04051e0be17347b09ef81a0849bec15a96e002e}"
PREFIX="$(mkdir -p "${1:-ffmpeg-vita}" && cd "${1:-ffmpeg-vita}" && pwd)"
WORK="${PREFIX}/.build"

if [ -f "${PREFIX}/lib/libavcodec.a" ] && [ -f "${PREFIX}/.theora-ok" ]; then
	echo "FFmpeg ${FFMPEG_VER} with Theora already built in ${PREFIX}"
	exit 0
fi

mkdir -p "${WORK}"
cd "${WORK}"

TARBALL="ffmpeg-${FFMPEG_VER}.tar.xz"
if [ ! -f "${TARBALL}" ]; then
	if command -v curl >/dev/null; then
		curl -fsSL -o "${TARBALL}" "https://ffmpeg.org/releases/${TARBALL}"
	else
		wget -q -O "${TARBALL}" "https://ffmpeg.org/releases/${TARBALL}"
	fi
fi
echo "${FFMPEG_SHA256}  ${TARBALL}" | sha256sum -c -

rm -rf "ffmpeg-${FFMPEG_VER}"
tar xf "${TARBALL}"
cd "ffmpeg-${FFMPEG_VER}"

CFLAGS_COMMON="-Wl,-q -O2 -ftree-vectorize -fomit-frame-pointer -ffast-math -D_BSD_SOURCE"
CFLAGS_WARN="-Wno-error=implicit-function-declaration -Wno-error=int-conversion -Wno-error=incompatible-pointer-types"

./configure --prefix="${PREFIX}" \
	--enable-cross-compile \
	--cross-prefix="${VITASDK}/bin/arm-vita-eabi-" \
	--disable-shared \
	--enable-static \
	--disable-runtime-cpudetect \
	--disable-armv5te \
	--disable-armv6t2 \
	--disable-programs \
	--disable-doc \
	--disable-network \
	--disable-everything \
	--enable-decoder=theora,vp3,vorbis,aac,adpcm_ima_qt,alac,ape,ac3,cinepak,flac,mp3,mpeg4,opus,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,svq1,tta,wmalossless,wavpack \
	--enable-demuxer=aac,ac3,aiff,ape,asf,avi,caf,cine,flac,matroska,mp3,mp4,m4a,pcm_s16le,pcm_s24le,pcm_s32le,pcm_s8,pcm_u8,ogg,opus,tta,wav,webm,wv \
	--enable-parser=aac,ac3,flac,h263,mpeg4video,mpegaudio,opus,vorbis \
	--enable-protocol=file \
	--enable-small \
	--disable-debug \
	--arch=armv7-a \
	--cpu=cortex-a9 \
	--target-os=none \
	--extra-cflags="-std=gnu11 ${CFLAGS_WARN} ${CFLAGS_COMMON}" \
	--extra-cxxflags="-fno-rtti -fno-exceptions -std=gnu++11 ${CFLAGS_WARN} ${CFLAGS_COMMON}" \
	--extra-ldflags="-L${VITASDK}/arm-vita-eabi/lib" \
	--disable-bzlib \
	--disable-iconv \
	--disable-lzma \
	--disable-sdl2 \
	--disable-securetransport \
	--disable-xlib \
	--enable-pthreads

# Fail early (and loudly) if the Theora decoder did not make it into the build.
grep -q "ff_theora_decoder" libavcodec/codec_list.c \
	|| { echo "error: Theora decoder not enabled in this FFmpeg build" >&2; exit 1; }

make -j"$(nproc)"
make install

touch "${PREFIX}/.theora-ok"
rm -rf "${WORK}"
echo "FFmpeg ${FFMPEG_VER} (with Theora) installed to ${PREFIX}"