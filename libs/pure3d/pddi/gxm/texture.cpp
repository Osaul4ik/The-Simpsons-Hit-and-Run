//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/device.hpp>
#include <pddi/gxm/display.hpp>
#include <pddi/gxm/texture.hpp>
#include <pddi/gxm/context.hpp>
#include <pddi/gxm/texture_swizzler.hpp>

#include <math.h>
#include <string.h>
#include <pddi/base/debug.hpp>
#include <radmemory.hpp>
#ifdef RAD_LOG
#include <raddebug.hpp>
#endif

#include <microprofile.h>
#include <arm_neon.h>

// Time spent on textures in the log (code/debug/vitaprof.cpp)
#ifdef RAD_LOG
#include <psp2/kernel/processmgr.h>
extern "C" void vitaProfAdd(const char* name, unsigned microseconds);
#endif

#include <p3d/fileftt.hpp>
#include <p3d/vitajobs.hpp>

// Palettized (4/8 bit) textures are stored natively as P8 with a 256 entry palette.
// The palette words are 0xAARRGGBB like pddiColour. If red and blue come out swapped on
// the device, build with -DGXM_PALETTE_TEXTURE_FORMAT=SCE_GXM_TEXTURE_FORMAT_P8_ABGR.
#ifndef GXM_PALETTE_TEXTURE_FORMAT
#define GXM_PALETTE_TEXTURE_FORMAT SCE_GXM_TEXTURE_FORMAT_P8_ARGB
#endif

// Opaque static RGB textures are stored as RGB565 (half the memory of RGB888).
// Build with -DRAD_GXM_TEXTURE_RGB565=0 to keep them 32 bit.
// Build with -DRAD_GXM_TEXTURE_P8=0 to expand palettized textures to 32 bit again.
#ifndef RAD_GXM_TEXTURE_P8
#define RAD_GXM_TEXTURE_P8 1
#endif

#ifndef RAD_GXM_TEXTURE_RGB565
#define RAD_GXM_TEXTURE_RGB565 1
#endif

// Static uncompressed textures are stored swizzled (better texture cache behaviour than
// linear) and get a generated mip chain (no shimmering on distant road/fences/buildings,
// about +33% memory per texture). Build with -DRAD_GXM_TEXTURE_MIPS=0 to keep only the base
// level or -DRAD_GXM_TEXTURE_SWIZZLE=0 to go back to linear textures (this also disables mips).
#ifndef RAD_GXM_TEXTURE_SWIZZLE
#define RAD_GXM_TEXTURE_SWIZZLE 1
#endif

#ifndef RAD_GXM_TEXTURE_MIPS
#define RAD_GXM_TEXTURE_MIPS 1
#endif

// Added to the level of detail of textures with a mip chain, in 1/8 level steps (GXM stores it as
// 31 + bias, valid range -31..32). There is no anisotropic filtering, so the sharper level is chosen
// from the compressed axis and a road seen at a low angle would go blurry right in front of the
// camera without this. Closer to 0: less shimmering, more blur. -8: base level until 2:1 minification.
#ifndef RAD_GXM_TEXTURE_LOD_BIAS_EIGHTHS
#define RAD_GXM_TEXTURE_LOD_BIAS_EIGHTHS -8
#endif
static_assert(RAD_GXM_TEXTURE_LOD_BIAS_EIGHTHS >= -31 && RAD_GXM_TEXTURE_LOD_BIAS_EIGHTHS <= 32, "GXM LOD bias out of range");

// Textures whose smaller side is below this many pixels get no mip chain: they hardly ever
// shimmer and the +33% memory is better spent elsewhere. Must be a power of two >= 16.
#ifndef RAD_GXM_TEXTURE_MIN_MIP_SIZE
#define RAD_GXM_TEXTURE_MIN_MIP_SIZE 64
#endif

// Keep the alpha-test coverage of cutout textures (fences, grates, foliage) constant across
// mip levels so they do not fade away with distance. Build with -DRAD_GXM_MIP_ALPHA_COVERAGE=0 to disable.
#ifndef RAD_GXM_MIP_ALPHA_COVERAGE
#define RAD_GXM_MIP_ALPHA_COVERAGE 1
#endif

static inline SceGxmTextureFormat PickPixelFormat(pddiPixelFormat format)
{
    switch (format)
    {
    case PDDI_PIXEL_PAL8: return GXM_PALETTE_TEXTURE_FORMAT;
    case PDDI_PIXEL_RGB565: return SCE_GXM_TEXTURE_FORMAT_U5U6U5_RGB;
    case PDDI_PIXEL_RGB888: return SCE_GXM_TEXTURE_FORMAT_X8U8U8U8_1RGB;
    case PDDI_PIXEL_ARGB8888: return SCE_GXM_TEXTURE_FORMAT_U8U8U8U8_ARGB;
    case PDDI_PIXEL_DXT1: return SCE_GXM_TEXTURE_FORMAT_UBC1_ABGR;
    case PDDI_PIXEL_DXT3: return SCE_GXM_TEXTURE_FORMAT_UBC2_ABGR;
    case PDDI_PIXEL_DXT5: return SCE_GXM_TEXTURE_FORMAT_UBC3_ABGR;
    case PDDI_TEXTYPE_YUV: return SCE_GXM_TEXTURE_FORMAT_YUV420P3_CSC0;
    }
    PDDIASSERT(false);
    return (SceGxmTextureFormat)0;
};

static inline pddiPixelFormat PickPixelFormat(pddiTextureType type, int bitDepth, int alphaDepth)
{
    switch (type)
    {
    case PDDI_TEXTYPE_RGB:
        switch (alphaDepth)
        {
        case 0:
            return (bitDepth <= 16) ? PDDI_PIXEL_RGB565 : PDDI_PIXEL_RGB888;
        case 1:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB1555 : PDDI_PIXEL_ARGB8888;
        default:
            return (bitDepth <= 16) ? PDDI_PIXEL_ARGB4444 : PDDI_PIXEL_ARGB8888;
        }
        break;

    case PDDI_TEXTYPE_PALETTIZED:
        return PDDI_PIXEL_PAL8;

    case PDDI_TEXTYPE_LUMINANCE:
        return PDDI_PIXEL_LUM8;

    case PDDI_TEXTYPE_BUMPMAP:
        return PDDI_PIXEL_DUDV88;

    case PDDI_TEXTYPE_DXT1:
        return PDDI_PIXEL_DXT1;

    case PDDI_TEXTYPE_DXT2:
        return PDDI_PIXEL_DXT2;

    case PDDI_TEXTYPE_DXT3:
        return PDDI_PIXEL_DXT3;

    case PDDI_TEXTYPE_DXT4:
        return PDDI_PIXEL_DXT4;

    case PDDI_TEXTYPE_DXT5:
        return PDDI_PIXEL_DXT5;

    case PDDI_TEXTYPE_YUV:
        return PDDI_PIXEL_YUV;
    }
    PDDIASSERT(false);
    return PDDI_PIXEL_UNKNOWN;
};

int fastlog2(int x)
{
    int r = 0;
    int tmp = x;
    while(tmp > 1)
    {
        r++;
        tmp = tmp >> 1;

        if((tmp << r) != x)
            // not power of 2
            return -1;
    }
    return r;
}

static inline bool IsDXT(pddiPixelFormat f)
{
    return f == PDDI_PIXEL_DXT1 || f == PDDI_PIXEL_DXT3 || f == PDDI_PIXEL_DXT5;
}

// GPU texture memory. radMemorySpace_Cdram already spills into the user block when
// CDRAM is full, and frees from the right heap.
static inline uint8_t* AllocGpuMem(size_t size, unsigned alignment)
{
    return (uint8_t*)radMemorySpaceAllocAligned(radMemorySpace_Cdram, radMemoryGetCurrentAllocator(), size, alignment);
}

static inline void FreeGpuMem(void* p)
{
    if(p)
        radMemorySpaceFreeAligned(radMemorySpace_Cdram, radMemoryGetCurrentAllocator(), p);
}

// ---------------------------------------------------------------------------------------
// Mip chain generation. Levels are tight (stride == width) and in memory order, i.e. the
// same row order Lock(0) hands out, so the box filter does not care about image orientation.
// ---------------------------------------------------------------------------------------
static inline void Box32Pixel(const uint8_t* a, const uint8_t* b, const uint8_t* c, const uint8_t* d, uint8_t* out)
{
    for(int k = 0; k < 4; k++)
        out[k] = (uint8_t)((a[k] + b[k] + c[k] + d[k] + 2) >> 2);
}

// 32 bit: every channel (alpha included) is the rounded mean of the 2x2 block. NEON handles
// 4 output pixels per iteration; the scalar tail gives bit-identical results.
static void Downsample32(const uint8_t* src, uint8_t* dst, int w, int h)
{
    const int dw = rmt::Max(w >> 1, 1), dh = rmt::Max(h >> 1, 1);
    const int sx = (w > 1) ? 1 : 0, sy = (h > 1) ? 1 : 0;
    for(int y = 0; y < dh; y++)
    {
        const uint8_t* r0 = src + size_t(y * 2) * w * 4;
        const uint8_t* r1 = src + size_t(y * 2 + sy) * w * 4;
        uint8_t* out = dst + size_t(y) * dw * 4;
        int x = 0;
        if(sx && sy)
        {
            const uint16x8_t two = vdupq_n_u16(2);
            for(; x + 4 <= dw; x += 4)
            {
                // even / odd pixels of 8 source pixels per row
                const uint32x4x2_t p0 = vld2q_u32((const uint32_t*)(r0 + x * 8));
                const uint32x4x2_t p1 = vld2q_u32((const uint32_t*)(r1 + x * 8));
                const uint8x16_t e0 = vreinterpretq_u8_u32(p0.val[0]), o0 = vreinterpretq_u8_u32(p0.val[1]);
                const uint8x16_t e1 = vreinterpretq_u8_u32(p1.val[0]), o1 = vreinterpretq_u8_u32(p1.val[1]);
                const uint16x8_t lo = vaddq_u16(vaddl_u8(vget_low_u8(e0), vget_low_u8(o0)),
                                                vaddl_u8(vget_low_u8(e1), vget_low_u8(o1)));
                const uint16x8_t hi = vaddq_u16(vaddl_u8(vget_high_u8(e0), vget_high_u8(o0)),
                                                vaddl_u8(vget_high_u8(e1), vget_high_u8(o1)));
                vst1q_u8(out + x * 4, vcombine_u8(vshrn_n_u16(vaddq_u16(lo, two), 2),
                                                  vshrn_n_u16(vaddq_u16(hi, two), 2)));
            }
        }
        for(; x < dw; x++)
        {
            const uint8_t* a = r0 + x * 2 * 4;
            const uint8_t* c = r1 + x * 2 * 4;
            Box32Pixel(a, a + sx * 4, c, c + sx * 4, out + x * 4);
        }
    }
}

// For textures with real alpha: the colour of (nearly) transparent texels is whatever the artist's
// tool left there, usually black or magenta. A plain average bleeds it into the visible edge, which
// gives cutouts a dark or coloured outline at a distance. Colour is weighted by alpha instead.
static void Downsample32AlphaWeighted(const uint8_t* src, uint8_t* dst, int w, int h)
{
    const int dw = rmt::Max(w >> 1, 1), dh = rmt::Max(h >> 1, 1);
    const int sx = (w > 1) ? 1 : 0, sy = (h > 1) ? 1 : 0;
    for(int y = 0; y < dh; y++)
    {
        const uint8_t* r0 = src + size_t(y * 2) * w * 4;
        const uint8_t* r1 = src + size_t(y * 2 + sy) * w * 4;
        uint8_t* out = dst + size_t(y) * dw * 4;
        for(int x = 0; x < dw; x++)
        {
            const uint8_t* p[4] = { r0 + x * 8, r0 + (x * 2 + sx) * 4, r1 + x * 8, r1 + (x * 2 + sx) * 4 };
            const uint32_t alphaSum = p[0][3] + p[1][3] + p[2][3] + p[3][3];
            if(alphaSum == 0 || alphaSum == 4 * 255)
            {
                // all the same alpha: nothing to weight
                Box32Pixel(p[0], p[1], p[2], p[3], out + x * 4);
                continue;
            }
            for(int c = 0; c < 3; c++)
            {
                const uint32_t sum = p[0][c] * p[0][3] + p[1][c] * p[1][3] + p[2][c] * p[2][3] + p[3][c] * p[3][3];
                out[x * 4 + c] = (uint8_t)((sum + alphaSum / 2) / alphaSum);
            }
            out[x * 4 + 3] = (uint8_t)((alphaSum + 2) >> 2);
        }
    }
}

static inline uint16_t Box565Pixel(uint16_t p0, uint16_t p1, uint16_t p2, uint16_t p3)
{
    const int r = ((p0 >> 11) & 0x1f) + ((p1 >> 11) & 0x1f) + ((p2 >> 11) & 0x1f) + ((p3 >> 11) & 0x1f);
    const int g = ((p0 >> 5) & 0x3f) + ((p1 >> 5) & 0x3f) + ((p2 >> 5) & 0x3f) + ((p3 >> 5) & 0x3f);
    const int b = (p0 & 0x1f) + (p1 & 0x1f) + (p2 & 0x1f) + (p3 & 0x1f);
    return (uint16_t)((((r + 2) >> 2) << 11) | (((g + 2) >> 2) << 5) | ((b + 2) >> 2));
}

// RGB565: NEON does 8 output pixels per iteration.
static void Downsample565(const uint8_t* src, uint8_t* dst, int w, int h)
{
    const int dw = rmt::Max(w >> 1, 1), dh = rmt::Max(h >> 1, 1);
    const int sx = (w > 1) ? 1 : 0, sy = (h > 1) ? 1 : 0;
    for(int y = 0; y < dh; y++)
    {
        const uint16_t* r0 = (const uint16_t*)src + size_t(y * 2) * w;
        const uint16_t* r1 = (const uint16_t*)src + size_t(y * 2 + sy) * w;
        uint16_t* out = (uint16_t*)dst + size_t(y) * dw;
        int x = 0;
        if(sx && sy)
        {
            const uint16x8_t m1f = vdupq_n_u16(0x1f), m3f = vdupq_n_u16(0x3f), two = vdupq_n_u16(2);
            for(; x + 8 <= dw; x += 8)
            {
                const uint16x8x2_t p0 = vld2q_u16(r0 + x * 2);
                const uint16x8x2_t p1 = vld2q_u16(r1 + x * 2);
                const uint16x8_t r = vaddq_u16(vaddq_u16(
                        vandq_u16(vshrq_n_u16(p0.val[0], 11), m1f), vandq_u16(vshrq_n_u16(p0.val[1], 11), m1f)),
                        vaddq_u16(vandq_u16(vshrq_n_u16(p1.val[0], 11), m1f), vandq_u16(vshrq_n_u16(p1.val[1], 11), m1f)));
                const uint16x8_t g = vaddq_u16(vaddq_u16(
                        vandq_u16(vshrq_n_u16(p0.val[0], 5), m3f), vandq_u16(vshrq_n_u16(p0.val[1], 5), m3f)),
                        vaddq_u16(vandq_u16(vshrq_n_u16(p1.val[0], 5), m3f), vandq_u16(vshrq_n_u16(p1.val[1], 5), m3f)));
                const uint16x8_t b = vaddq_u16(vaddq_u16(
                        vandq_u16(p0.val[0], m1f), vandq_u16(p0.val[1], m1f)),
                        vaddq_u16(vandq_u16(p1.val[0], m1f), vandq_u16(p1.val[1], m1f)));
                const uint16x8_t r2 = vshrq_n_u16(vaddq_u16(r, two), 2);
                const uint16x8_t g2 = vshrq_n_u16(vaddq_u16(g, two), 2);
                const uint16x8_t b2 = vshrq_n_u16(vaddq_u16(b, two), 2);
                vst1q_u16(out + x, vorrq_u16(vorrq_u16(vshlq_n_u16(r2, 11), vshlq_n_u16(g2, 5)), b2));
            }
        }
        for(; x < dw; x++)
            out[x] = Box565Pixel(r0[x * 2], r0[x * 2 + sx], r1[x * 2], r1[x * 2 + sx]);
    }
}

#if RAD_GXM_MIP_ALPHA_COVERAGE
// Alpha-tested ("cutout") textures lose coverage when averaged: thin bars of a fence fade to
// alpha < 0.5 and vanish at distance. Rescale each level's alpha so that the share of texels
// passing the usual 0.5 alpha test matches level 0. Only done for textures whose alpha is
// (nearly) binary, so smooth translucency (glass, smoke) is left alone.
static void AlphaHistogram(const uint8_t* px, size_t n, uint32_t* hist)
{
    memset(hist, 0, 256 * sizeof(uint32_t));
    for(size_t i = 0; i < n; i++)
        hist[px[i * 4 + 3]]++;
}

static inline float AlphaCoverage(const uint32_t* hist, size_t n)
{
    size_t pass = 0;
    for(int a = 128; a < 256; a++)
        pass += hist[a];
    return n ? float(pass) / float(n) : 0.0f;
}

// true if most texels are (nearly) fully transparent or opaque and some are transparent
static bool IsCutoutAlpha(const uint32_t* hist, size_t n)
{
    size_t mid = 0;
    for(int a = 32; a < 224; a++)
        mid += hist[a];
    size_t clear = 0;
    for(int a = 0; a < 128; a++)
        clear += hist[a];
    return n && clear > 0 && mid * 10 <= n;
}

static void PreserveAlphaCoverage(uint8_t* px, size_t n, float target)
{
    uint32_t hist[256];
    AlphaHistogram(px, n, hist);
    if(target <= 0.0f || n == 0)
        return;

    // Try scales 2^(k/8) for k in [-8,16] (0.5 .. 4) and keep the one whose coverage is closest
    // to the target; on ties prefer the scale nearest 1. Picking the closest (instead of "at
    // least the target") avoids turning a thin fence into a solid wall when the alpha of a
    // level is nearly flat and coverage jumps from 0 to 1.
    float hi = 1.0f, bestErr = 2.0f;
    for(int step = 0; step <= 32; step++)
    {
        const int k = (step & 1) ? -((step + 1) >> 1) : (step >> 1);    // 0,-1,1,-2,2,... (nearest 1 first)
        if(k < -8 || k > 16)
            continue;
        const float scale = powf(2.0f, k / 8.0f);
        size_t pass = 0;
        for(int a = 0; a < 256; a++)
            if(a * scale >= 127.5f)
                pass += hist[a];
        const float err = fabsf(float(pass) / float(n) - target);
        if(err < bestErr - 1e-6f)
        {
            bestErr = err;
            hi = scale;
        }
    }
    uint8_t lut[256];
    for(int a = 0; a < 256; a++)
        lut[a] = (uint8_t)rmt::Min(255, int(a * hi + 0.5f));
    for(size_t i = 0; i < n; i++)
        px[i * 4 + 3] = lut[px[i * 4 + 3]];
}
#endif

// Index of the palette entry closest to an 0xAARRGGBB colour (sum of absolute differences; alpha
// counts four times, a wrong alpha shows much more than a slightly wrong colour).
static inline uint8_t NearestPaletteEntry(const uint32_t* pal, int entries, uint32_t colour)
{
    const uint8x16_t target = vreinterpretq_u8_u32(vdupq_n_u32(colour));
    uint32_t bestDist = 0xffffffffu;
    int best = 0;
    int i = 0;
    for(; i + 4 <= entries; i += 4)
    {
        const uint8x16_t diff = vabdq_u8(vld1q_u8((const uint8_t*)(pal + i)), target);
        uint32x4_t sum = vpaddlq_u16(vpaddlq_u8(diff));
        sum = vmlaq_n_u32(sum, vshrq_n_u32(vreinterpretq_u32_u8(diff), 24), 3);
        uint32_t d[4];
        vst1q_u32(d, sum);
        for(int k = 0; k < 4; k++)
            if(d[k] < bestDist) { bestDist = d[k]; best = i + k; }
    }
    for(; i < entries; i++)
    {
        uint32_t dist = 0;
        for(int c = 0; c < 4; c++)
        {
            const int delta = (int)((pal[i] >> (c * 8)) & 0xff) - (int)((colour >> (c * 8)) & 0xff);
            dist += (uint32_t)(delta < 0 ? -delta : delta) * (c == 3 ? 4 : 1);
        }
        if(dist < bestDist) { bestDist = dist; best = i; }
    }
    return (uint8_t)best;
}

// Palette indices cannot be averaged directly: average the colours of each 2x2 block (weighted
// by alpha, like Downsample32AlphaWeighted) and store the palette entry closest to the result.
// Just picking one of the four texels turns a mip level into a noisy point sample, which is what
// made palettized surfaces look grainy. Blocks that round to the same colour share one palette
// search through a small cache, so this stays cheap enough for level streaming.
static void DownsampleP8(const uint8_t* src, uint8_t* dst, int w, int h, const uint32_t* pal, int entries)
{
    enum { CACHE_BITS = 9, CACHE_SIZE = 1 << CACHE_BITS };
    uint32_t cacheKey[CACHE_SIZE];
    uint8_t cacheIndex[CACHE_SIZE];
    memset(cacheKey, 0xff, sizeof(cacheKey));   // keys have their low bits clear: never all ones

    const int dw = rmt::Max(w >> 1, 1), dh = rmt::Max(h >> 1, 1);
    const int sx = (w > 1) ? 1 : 0, sy = (h > 1) ? 1 : 0;
    for(int y = 0; y < dh; y++)
    {
        const uint8_t* r0 = src + size_t(y * 2) * w;
        const uint8_t* r1 = src + size_t(y * 2 + sy) * w;
        for(int x = 0; x < dw; x++)
        {
            const uint8_t i0 = r0[x * 2], i1 = r0[x * 2 + sx], i2 = r1[x * 2], i3 = r1[x * 2 + sx];
            if(i0 == i1 && i0 == i2 && i0 == i3)
            {
                dst[y * dw + x] = i0;
                continue;
            }
            const uint32_t c[4] = { pal[i0], pal[i1], pal[i2], pal[i3] };
            const uint32_t alphaSum = (c[0] >> 24) + (c[1] >> 24) + (c[2] >> 24) + (c[3] >> 24);
            uint32_t r = 0, g = 0, b = 0;
            for(int k = 0; k < 4; k++)
            {
                const uint32_t weight = alphaSum ? (c[k] >> 24) : 1;
                r += ((c[k] >> 16) & 0xff) * weight;
                g += ((c[k] >> 8) & 0xff) * weight;
                b += (c[k] & 0xff) * weight;
            }
            const uint32_t total = alphaSum ? alphaSum : 4;
            const uint32_t colour = (((alphaSum + 2) >> 2) << 24) | (((r + total / 2) / total) << 16) |
                                    (((g + total / 2) / total) << 8) | ((b + total / 2) / total);

            const uint32_t key = colour & 0xf8f8f8f8u;
            const uint32_t slot = (key * 2654435761u) >> (32 - CACHE_BITS);
            if(cacheKey[slot] != key)
            {
                cacheKey[slot] = key;
                cacheIndex[slot] = NearestPaletteEntry(pal, entries, colour);
            }
            dst[y * dw + x] = cacheIndex[slot];
        }
    }
}

static void SwizzleLevel(int bytesPerPixel, uint8_t* dst, uint8_t* src, int w, int h)
{
    const uint32_t tile = (uint32_t)rmt::Min(w, h);
    switch(bytesPerPixel)
    {
    case 1: SwizzleTexData8Bpp(dst, src, 0, 0, w, h, w, tile); break;
    case 2: SwizzleTexData16Bpp(dst, src, 0, 0, w, h, w, tile); break;
    default: SwizzleTexData32Bpp(dst, src, 0, 0, w, h, w, tile); break;
    }
}

// Everything a mip chain upload needs. Run on a worker core while the loader hands the main thread
// its frames back, so it only touches this data and allocates nothing (the game's heaps are not
// safe to use from a thread other than the main and loader threads, which take turns).
struct MipChainJob
{
    const uint8_t* level0;
    const uint32_t* palette;
    int paletteEntries;
    int firstLevel;
    uint8_t* bits;
    uint8_t* scratch;       // room for all generated levels, or NULL
    int xSize, ySize, mipCount, bpp8;
    bool p8, hasAlpha;
    int levelsDone;         // result
    unsigned microseconds;  // result, for the log
};

static void RunMipChainJob(void* arg)
{
    MipChainJob& j = *(MipChainJob*)arg;
#ifdef RAD_LOG
    const unsigned started = sceKernelGetProcessTimeLow();
#endif
    int w = j.xSize, h = j.ySize;
    size_t offset = 0;
    const uint8_t* cur = j.level0;
    uint8_t* next = j.scratch;
    bool anyAlpha = false;      // some texel is not fully opaque
    int levelsDone = 0;

#if RAD_GXM_MIP_ALPHA_COVERAGE
    bool cutout = false;
    float coverage = 0.0f;
#endif
    if(j.hasAlpha && j.bpp8 == 4 && j.mipCount > 1 && j.firstLevel == 0)
    {
        uint32_t hist[256];
        AlphaHistogram(j.level0, size_t(j.xSize) * j.ySize, hist);
        anyAlpha = hist[255] != size_t(j.xSize) * j.ySize;
#if RAD_GXM_MIP_ALPHA_COVERAGE
        cutout = IsCutoutAlpha(hist, size_t(j.xSize) * j.ySize);
        coverage = AlphaCoverage(hist, size_t(j.xSize) * j.ySize);
#endif
    }

    for(int l = 0; l < j.mipCount; l++)
    {
        if(l >= j.firstLevel)
        {
            SwizzleLevel(j.bpp8, j.bits + offset, (uint8_t*)cur, w, h);
            levelsDone = l + 1;
        }
        offset += size_t(w) * h * j.bpp8;

        // Out of scratch memory (or P8 without its palette yet): use the levels we have.
        if(l + 1 >= j.mipCount || next == NULL)
            break;

        const int nw = rmt::Max(w >> 1, 1), nh = rmt::Max(h >> 1, 1);
        if(j.p8)
            DownsampleP8(cur, next, w, h, j.palette, j.paletteEntries);
        else if(j.bpp8 == 2)
            Downsample565(cur, next, w, h);
        else
        {
            if(anyAlpha)
                Downsample32AlphaWeighted(cur, next, w, h);
            else
                Downsample32(cur, next, w, h);
#if RAD_GXM_MIP_ALPHA_COVERAGE
            if(cutout)
                PreserveAlphaCoverage(next, size_t(nw) * nh, coverage);
#endif
        }

        cur = next;
        next += size_t(nw) * nh * j.bpp8;
        w = nw;
        h = nh;
    }

    j.levelsDone = levelsDone;
#ifdef RAD_LOG
    j.microseconds = sceKernelGetProcessTimeLow() - started;
#endif
}

// Below this a job is not worth it: the loader would lose the rest of its turn for almost nothing.
#ifndef RAD_GXM_TEXTURE_JOB_MIN_BYTES
#define RAD_GXM_TEXTURE_JOB_MIN_BYTES ( 16 * 1024 )
#endif

// Runs a job on a worker core when called by the loader: the main thread keeps running meanwhile.
// Anywhere else it runs right here, since only the loader can wait without stopping the game.
static void RunTextureJob(vitaJobFunction function, void* arg, size_t bytes)
{
    vitaJob job;
    if(bytes >= RAD_GXM_TEXTURE_JOB_MIN_BYTES && tFileFTT::IsLoaderThread() && vitaJobSubmit(&job, function, arg))
        tFileFTT::WaitForJob(&job);
    else
        function(arg);
}

// Writes level 0 (always) and the generated levels >= firstLevel (and < mipCount) into the
// swizzled texture. Levels are stored back to back, each w*h*bytes, largest first.
// palette is only used for P8.
void gxmTexture::UploadChain(const uint8_t* level0, const uint32_t* palette, int paletteEntries, int firstLevel)
{
    MipChainJob job;
    job.level0 = level0;
    job.palette = palette;
    job.paletteEntries = paletteEntries;
    job.firstLevel = firstLevel;
    job.bits = bits;
    job.scratch = NULL;
    job.xSize = xSize;
    job.ySize = ySize;
    job.mipCount = mipCount;
    job.bpp8 = lock.depth / 8;
    job.p8 = (lock.format == PDDI_PIXEL_PAL8);
    job.hasAlpha = hasAlpha;
    job.levelsDone = 0;
    job.microseconds = 0;

    // Scratch memory for levels 1.. (only needed when there is something to generate).
    if(mipCount > 1 && !(job.p8 && palette == NULL))
    {
        size_t scratchBytes = 0;
        for(int l = 1; l < mipCount; l++)
            scratchBytes += size_t(rmt::Max(xSize >> l, 1)) * rmt::Max(ySize >> l, 1) * job.bpp8;
        job.scratch = (uint8_t*)radMemoryAllocAligned(radMemoryGetCurrentAllocator(), scratchBytes, 16);
    }

    RunTextureJob(RunMipChainJob, &job, size_t(xSize) * ySize * job.bpp8);

    if(job.scratch)
        radMemoryFreeAligned(job.scratch);

#ifdef RAD_LOG
    vitaProfAdd("Texture: swizzle+mips work", job.microseconds);
#endif

    int levelsDone = job.levelsDone;
    if(levelsDone < 1)
        levelsDone = 1;
    if(levelsDone != validMips)
    {
        validMips = levelsDone;
        sampler.valid = false;
        CHK_GXM(sceGxmTextureSetMipmapCount(&texture, validMips));
    }
}

struct DxtSwizzleJob
{
    uint8_t* bits;
    uint8_t* source;
    uint32_t width, height;
    bool blocks16;
};

static void RunDxtSwizzleJob(void* arg)
{
    DxtSwizzleJob& j = *(DxtSwizzleJob*)arg;
    if(j.blocks16)
        SwizzleTexData128Bpp(j.bits, j.source, 0, 0, j.width, j.height, j.width, rmt::Min(j.width, j.height));
    else
        SwizzleTexData64Bpp(j.bits, j.source, 0, 0, j.width, j.height, j.width, rmt::Min(j.width, j.height));
}

// P8: the mip chain needs the palette, which the loader supplies after Unlock(0).
void gxmTexture::BuildPaletteMips()
{
    if(pendingLinear == NULL || paletteMem == NULL || nPalette == 0)
        return;
    UploadChain((const uint8_t*)pendingLinear, paletteMem, nPalette, 1);
    radMemoryFreeAligned(pendingLinear);
    pendingLinear = NULL;
}

bool gxmTexture::Create(int x, int y, int bpp, int alphaDepth, int nMip, pddiTextureType textureType, pddiTextureUsageHint usageHint)
{
    xSize = x;
    ySize = y;
    nMipMap = nMip;
    type = textureType;

    log2X = fastlog2(xSize);
    log2Y = fastlog2(ySize);

    if((log2X == -1) || (log2Y == -1))
    {
        if(type == PDDI_TEXTYPE_DXT1 || type == PDDI_TEXTYPE_DXT3 || type == PDDI_TEXTYPE_DXT5)
        {
            lastError = PDDI_TEX_NOT_POW_2;
            return false;
        }
    }

    if ((xSize > context->GetMaxTextureDimension()) ||
        (ySize > context->GetMaxTextureDimension()))
    {
        lastError = PDDI_TEX_TOO_BIG;
        return false;
    }

    // 4 and 8 bit palettized textures are both stored as P8 (indices one per byte).
    bool palettized = (textureType == PDDI_TEXTYPE_PALETTIZED);
#if !RAD_GXM_TEXTURE_P8
    if (palettized)
    {
        // old behaviour: the loader depalettizes into a 32 bit RGB texture
        palettized = false;
        textureType = PDDI_TEXTYPE_RGB;
        bpp = 32;
    }
#endif
    opaquePalette = (alphaDepth == 0);
    if (palettized)
    {
        bpp = 8;
    }
    else if (textureType == PDDI_TEXTYPE_RGB)
    {
        // 16 bit formats with alpha have no lock path here, keep them 32 bit.
        if (bpp <= 16 && alphaDepth > 0)
            bpp = 32;
#if RAD_GXM_TEXTURE_RGB565
        else if (alphaDepth == 0 && usageHint == PDDI_USAGE_STATIC)
            bpp = 16;
#endif
    }

    lock.depth = bpp;
    lock.format = palettized ? PDDI_PIXEL_PAL8 : PickPixelFormat(textureType, bpp, alphaDepth);
    lock.palette = NULL;

    const bool compressed = IsDXT(lock.format);
    const bool yuv = (lock.format == PDDI_PIXEL_YUV);

    // Static power-of-two uncompressed textures (min side >= 8) are stored swizzled with a
    // mip chain whose smallest level is 8 pixels (so every level is a plain w*h*bytes block).
    swizzled = false;
    mipCount = 1;
    validMips = 1;
    hasAlpha = (alphaDepth > 0 && bpp == 32 && !palettized);
#if RAD_GXM_TEXTURE_SWIZZLE
    if(!compressed && !yuv && usageHint == PDDI_USAGE_STATIC &&
       log2X >= 3 && log2Y >= 3 &&
       ((palettized && bpp == 8) || (textureType == PDDI_TEXTYPE_RGB && (bpp == 16 || bpp == 32))))
    {
        swizzled = true;
#if RAD_GXM_TEXTURE_MIPS
        const int minLog = rmt::Min(log2X, log2Y);
        if(rmt::Min(xSize, ySize) >= RAD_GXM_TEXTURE_MIN_MIP_SIZE && minLog >= 4)
            mipCount = minLog - 2;
#endif
    }
#endif

    size_t size;
    if(swizzled)
    {
        size = 0;
        for(int l = 0; l < mipCount; l++)
            size += size_t(rmt::Max(xSize >> l, 1)) * rmt::Max(ySize >> l, 1) * (bpp / 8);
        stride = (xSize * bpp) / 8;
    }
    else if(compressed)
    {
        const unsigned int blocksize = type == PDDI_TEXTYPE_DXT1 ? 8 : 16;
        size = size_t(ceil(double(xSize)/4)*ceil(double(ySize)/4)*blocksize);
        stride = 0;
    }
    else if(yuv)
    {
        size = size_t((xSize*ySize*bpp)/8);
        stride = (xSize*bpp)/8;
    }
    else
    {
        // linear textures are padded to a multiple of 8 pixels per row; the real value
        // is read back from GXM below
        stride = ((xSize + 7) & ~7) * (bpp / 8);
        size = size_t(stride) * ySize;
    }

    // Previously a failed allocation returned NULL, which was stored unchecked and gave
    // transparent/garbage textures. Fail cleanly if both CDRAM and user memory are full.
    radMemorySetAllocationName("gxmTexture");
    for(int attempt = 0; attempt < 2; attempt++)
    {
        bits = AllocGpuMem(size, SCE_GXM_TEXTURE_ALIGNMENT);
        if(bits == NULL)
            break;

        sampler.valid = false;
        if(swizzled)
        {
            CHK_GXM(sceGxmTextureInitSwizzled(&texture, bits, PickPixelFormat(lock.format), xSize, ySize, mipCount));
            if(mipCount > 1)
                CHK_GXM(sceGxmTextureSetLodBias(&texture, 31 + RAD_GXM_TEXTURE_LOD_BIAS_EIGHTHS));
            // mips are filled when level 0 is unlocked (P8: when the palette arrives)
            validMips = (palettized && mipCount > 1) ? 1 : mipCount;
            if(validMips != mipCount)
                CHK_GXM(sceGxmTextureSetMipmapCount(&texture, validMips));
            break;
        }

        if(compressed)
            CHK_GXM(sceGxmTextureInitSwizzled(&texture, bits, PickPixelFormat(lock.format), xSize, ySize, 0));
        else
            CHK_GXM(sceGxmTextureInitLinear(&texture, bits, PickPixelFormat(lock.format), xSize, ySize, 0));

        if(compressed || yuv)
            break;

        // sceGxmTextureGetStride is only documented for linear-strided textures, so don't
        // trust it blindly: accept it only when it is a plausible row size, otherwise keep
        // the 8-pixel-aligned stride that GXM uses for plain linear textures.
        const int realStride = (int)sceGxmTextureGetStride(&texture);
        const int minStride = xSize * (bpp / 8);
#ifdef RAD_LOG
        static int s_strideLogs = 0;
        if(s_strideLogs++ < 12)
            rReleasePrintf("[gxm] texture %dx%d bpp=%d fmt=%d stride guess=%d gxm=%d\n", xSize, ySize, bpp, (int)lock.format, stride, realStride);
#endif
        if(realStride < minStride || (realStride & 3) != 0 || realStride > stride * 2 || realStride == stride)
            break;

        if(size_t(realStride) * ySize <= size)
        {
            // smaller but valid: GPU reads rows at this pitch, so write them at it too
            stride = realStride;
            break;
        }

        // our guess was too small: reallocate with the stride GXM actually uses
        FreeGpuMem(bits);
        bits = NULL;
        stride = realStride;
        size = size_t(stride) * ySize;
    }

    if(bits && palettized)
    {
        paletteMem = (uint32_t*)AllocGpuMem(256 * sizeof(uint32_t), SCE_GXM_PALETTE_ALIGNMENT);
        if(paletteMem == NULL)
        {
            FreeGpuMem(bits);
            bits = NULL;
        }
        else
        {
            memset(paletteMem, 0, 256 * sizeof(uint32_t));
            lock.palette = paletteMem;
            nPalette = 0;
            CHK_GXM(sceGxmTextureSetPalette(&texture, paletteMem));
        }
    }

    if(bits == NULL)
    {
        lastError = PDDI_TEX_OUT_OF_MEMORY;
#ifdef RAD_LOG
        rReleasePrintf("[gxm] texture %dx%d alloc FAILED in CDRAM and user memory\n", xSize, ySize);
#endif
        return false;
    }

    if(lock.format == PDDI_PIXEL_RGB565)
    {
        // MakeColour() packs 0xAARRGGBB into 565; the alpha bits land above bit 15 and
        // are dropped by the 16 bit store.
        lock.native = false;
        lock.rgbaRShift[0] = 19; lock.rgbaLShift[0] = 11;
        lock.rgbaRShift[1] = 10; lock.rgbaLShift[1] = 5;
        lock.rgbaRShift[2] = 3;  lock.rgbaLShift[2] = 0;
        lock.rgbaRShift[3] = 24; lock.rgbaLShift[3] = 16;

        lock.rgbaMask[0] = 0x0000f800;
        lock.rgbaMask[1] = 0x000007e0;
        lock.rgbaMask[2] = 0x0000001f;
        lock.rgbaMask[3] = 0;
    }
    else if(context->GetDisplay()->ExtBGRA())
    {
        lock.native = true;
        lock.rgbaLShift[0] = lock.rgbaRShift[0] =
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaLShift[2] = lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x00ff0000;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x000000ff;
        lock.rgbaMask[3] = 0xff000000;
    }
    else
    {
        lock.native = false;
        lock.rgbaRShift[0] = 16;
        lock.rgbaLShift[2] = 16;

        lock.rgbaLShift[0] = 
        lock.rgbaLShift[1] = lock.rgbaRShift[1] =
        lock.rgbaRShift[2] =
        lock.rgbaLShift[3] = lock.rgbaRShift[3] = 0;

        lock.rgbaMask[0] = 0x000000ff;
        lock.rgbaMask[1] = 0x0000ff00;
        lock.rgbaMask[2] = 0x00ff0000;
        lock.rgbaMask[3] = 0xff000000;
    }

    statKB = (float)size / 1024.0f;
    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, statKB);
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, 1);

    return true;
}

gxmTexture::gxmTexture(gxmContext* c)
{
    context = c;
    contextID = c->contextID;
    bits = NULL;
    stride = 0;
    lockTemp = NULL;
    paletteMem = NULL;
    nPalette = 0;
    opaquePalette = false;
    sampler.valid = false;
    swizzled = false;
    hasAlpha = false;
    mipCount = 1;
    validMips = 1;
    pendingLinear = NULL;
    statKB = 0.0f;
    nMipMap = 0;
    xSize = ySize = 0;
    priority = 15;
}

gxmTexture::~gxmTexture()
{
    if(lockTemp)
        radMemoryFreeAligned(lockTemp);
    if(pendingLinear)
        radMemoryFreeAligned(pendingLinear);
    FreeGpuMem(bits);
    FreeGpuMem(paletteMem);

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -statKB);
    context->ADD_STAT(PDDI_STAT_TEXTURE_COUNT_32BIT, -1);
}

pddiPixelFormat gxmTexture::GetPixelFormat()
{
    // lets tImageConverter know it has to upload the palette
    if(lock.format == PDDI_PIXEL_PAL8)
        return PDDI_PIXEL_PAL8;
    return PDDI_PIXEL_ARGB8888;
}

int   gxmTexture::GetWidth()
{
    return xSize;
}

int   gxmTexture::GetHeight()
{
    return ySize;
}

int   gxmTexture::GetDepth()
{
    return 32;
}

int   gxmTexture::GetNumMipMaps()
{
    return nMipMap;
}

int gxmTexture::GetAlphaDepth()
{
    return 8;
}

pddiLockInfo* gxmTexture::Lock(int mipMap, pddiRect* rect)
{
    PDDIASSERT(mipMap <= nMipMap);

    if(lockTemp)
    {
        radMemoryFreeAligned(lockTemp);
        lockTemp = NULL;
    }

    lock.width = rmt::Max(xSize >> mipMap, 1);
    lock.height = rmt::Max(ySize >> mipMap, 1);

    if (IsDXT(lock.format))
    {
        unsigned int blocksize = lock.format == PDDI_PIXEL_DXT1 ? 8 : 16;
        lock.pitch = ceil( double( xSize >> mipMap ) / 4 ) * blocksize;
        lockTemp = radMemoryAllocAligned(radMemoryGetCurrentAllocator(), size_t(ceil(double(ySize>>mipMap)/4)*lock.pitch), 16);
        lock.bits = lockTemp;
    }
    else if (lock.format == PDDI_PIXEL_YUV)
    {
        lock.pitch = (lock.width * lock.depth) / 8;
        lock.bits = bits;
    }
    else if (swizzled && mipMap == 0)
    {
        // Rows are written into a tight scratch copy (bottom-up, like the linear path) and
        // swizzled into the texture by Unlock(0). The mip chain is generated from it there.
        const int rowBytes = (xSize * lock.depth) / 8;
        lockTemp = radMemoryAllocAligned(radMemoryGetCurrentAllocator(), size_t(rowBytes) * ySize, 16);
        lock.pitch = -rowBytes;
        lock.bits = (uint8_t*)lockTemp + size_t(rowBytes) * (lock.height - 1);
    }
    else if (mipMap == 0)
    {
        // bottom-up rows, written straight into the texture
        lock.pitch = -stride;
        lock.bits = bits + size_t(stride) * (lock.height - 1);
    }
    else
    {
        // higher levels are generated from level 0: the caller writes into a scratch buffer
        // that Unlock() throws away
        const int rowBytes = (lock.width * lock.depth) / 8;
        lockTemp = radMemoryAllocAligned(radMemoryGetCurrentAllocator(), size_t(rowBytes) * lock.height, 16);
        lock.pitch = -rowBytes;
        lock.bits = (uint8_t*)lockTemp + size_t(rowBytes) * (lock.height - 1);
    }

    return &lock;
}

void gxmTexture::Unlock(int mipLevel)
{
    if(lockTemp == NULL)
        return;

    if(mipLevel == 0 && swizzled)
    {
        if(lock.format == PDDI_PIXEL_PAL8)
        {
            // level 0 now; the other levels once the palette is known (immediately if it already is)
            if(pendingLinear)
                radMemoryFreeAligned(pendingLinear);
            UploadChain((const uint8_t*)lockTemp, NULL, 0, 0);
            pendingLinear = lockTemp;
            lockTemp = NULL;
            if(nPalette > 0)
                BuildPaletteMips();
            return;
        }
        UploadChain((const uint8_t*)lockTemp, NULL, 0, 0);
    }
    else if(mipLevel == 0 && IsDXT(lock.format))
    {
        uint32_t width = ceil(double(xSize) / 4);
        uint32_t height = ceil(double(ySize) / 4);
        // DXT1 has blocks of 8 bytes, DXT3 and DXT5 of 16. DXT3 was swizzled as if 8: the mission pictures.
        DxtSwizzleJob job = { bits, (uint8_t*)lockTemp, width, height, lock.format != PDDI_PIXEL_DXT1 };
        RunTextureJob(RunDxtSwizzleJob, &job, size_t(width) * height * (job.blocks16 ? 16 : 8));
    }

    radMemoryFreeAligned(lockTemp);
    lockTemp = NULL;
}

void gxmTexture::ApplySamplerState(SceGxmTextureFilter mag, SceGxmTextureFilter min,
                                   SceGxmTextureAddrMode u, SceGxmTextureAddrMode v,
                                   int mips, SceGxmTextureMipFilter mipFilter)
{
    const bool useMips = mips > 0;   // 0: texture has no mip chain, leave its mip state alone
    const bool all = !sampler.valid;

    if(all || sampler.mag != (uint8_t)mag)
    {
        CHK_GXM(sceGxmTextureSetMagFilter(&texture, mag));
        sampler.mag = (uint8_t)mag;
    }
    if(all || sampler.min != (uint8_t)min)
    {
        CHK_GXM(sceGxmTextureSetMinFilter(&texture, min));
        sampler.min = (uint8_t)min;
    }
    if(all || sampler.u != (uint8_t)u)
    {
        CHK_GXM(sceGxmTextureSetUAddrMode(&texture, u));
        sampler.u = (uint8_t)u;
    }
    if(all || sampler.v != (uint8_t)v)
    {
        CHK_GXM(sceGxmTextureSetVAddrMode(&texture, v));
        sampler.v = (uint8_t)v;
    }
    if(useMips)
    {
        if(all || sampler.mipCount != mips)
        {
            CHK_GXM(sceGxmTextureSetMipmapCount(&texture, mips));
            sampler.mipCount = mips;
        }
        if(all || sampler.mipFilter != (uint8_t)mipFilter)
        {
            CHK_GXM(sceGxmTextureSetMipFilter(&texture, mipFilter));
            sampler.mipFilter = (uint8_t)mipFilter;
        }
    }
    else if(all)
    {
        // never touched: remember the values so the next compare is meaningful
        sampler.mipCount = 0;
        sampler.mipFilter = 0xff;
    }
    sampler.valid = true;
}

void gxmTexture::SetPriority(int p)
{
    priority = p;
}

int gxmTexture::GetPriority(void)
{
    return priority;
}

// paging control
void gxmTexture::Prefetch(void)
{
}

void gxmTexture::Discard(void)
{
}

// palette managment
int gxmTexture::GetNumPaletteEntries(void)
{
    return nPalette;
}

void gxmTexture::SetPalette(int nEntries, pddiColour* palette)
{
    if(paletteMem == NULL || palette == NULL)
        return;

    if(nEntries > 256)
        nEntries = 256;

    const uint32_t* src = (const uint32_t*)palette;
    const uint32_t alpha = opaquePalette ? 0xff000000 : 0;
    for(int i = 0; i < nEntries; i++)
        paletteMem[i] = src[i] | alpha;
    nPalette = nEntries;

    // P8 mip levels are chosen from the palette colours, so build them now
    BuildPaletteMips();
}

int gxmTexture::GetPalette(pddiColour* palette)
{
    if(paletteMem == NULL || palette == NULL)
        return 0;

    memcpy(palette, paletteMem, nPalette * sizeof(uint32_t));
    return nPalette;
}