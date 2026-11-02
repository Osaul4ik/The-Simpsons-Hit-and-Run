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

    // Only the base level lives on the GPU.
    size_t size;
    if(compressed)
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

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, (float)((xSize * ySize * lock.depth) / 8192));
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
    nMipMap = 0;
    xSize = ySize = 0;
    priority = 15;
}

gxmTexture::~gxmTexture()
{
    if(lockTemp)
        radMemoryFreeAligned(lockTemp);
    FreeGpuMem(bits);
    FreeGpuMem(paletteMem);

    context->ADD_STAT(PDDI_STAT_TEXTURE_ALLOC_32BIT, -(float)((xSize * ySize * lock.depth) / 8192));
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
    else if (mipMap == 0)
    {
        // bottom-up rows, written straight into the texture
        lock.pitch = -stride;
        lock.bits = bits + size_t(stride) * (lock.height - 1);
    }
    else
    {
        // higher levels are not stored: the caller writes into a scratch buffer that
        // Unlock() throws away
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

    if(mipLevel == 0 && IsDXT(lock.format))
    {
        uint32_t width = ceil(double(xSize) / 4);
        uint32_t height = ceil(double(ySize) / 4);
        if(type == PDDI_TEXTYPE_DXT5)
            SwizzleTexData128Bpp(bits, (uint8_t*)lockTemp, 0, 0, width, height, width, rmt::Min(width, height));
        else
            SwizzleTexData64Bpp(bits, (uint8_t*)lockTemp, 0, 0, width, height, width, rmt::Min(width, height));
    }

    radMemoryFreeAligned(lockTemp);
    lockTemp = NULL;
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
}

int gxmTexture::GetPalette(pddiColour* palette)
{
    if(paletteMem == NULL || palette == NULL)
        return 0;

    memcpy(palette, paletteMem, nPalette * sizeof(uint32_t));
    return nPalette;
}