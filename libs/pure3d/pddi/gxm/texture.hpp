//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _TEXTURE_HPP_
#define _TEXTURE_HPP_
                 
#include <pddi/pddi.hpp>
#include <pddi/pdditype.hpp>
class gxmContext;
         
class gxmTexture : public pddiTexture
{
public:
    gxmTexture(gxmContext*);
    ~gxmTexture();

    bool Create(int xSize, int ySize, int bpp, int alphaDepth, int nMip, pddiTextureType type = PDDI_TEXTYPE_RGB, pddiTextureUsageHint usageHint = PDDI_USAGE_STATIC);

    pddiPixelFormat GetPixelFormat();
    int GetWidth();
    int GetHeight();
    int GetDepth();
    int GetNumMipMaps();
    int GetAlphaDepth();

    // palette managment
    int GetNumPaletteEntries(void);
    void SetPalette(int nEntries, pddiColour* palette);
    int GetPalette(pddiColour* palette);

    // texture upload
    pddiLockInfo* Lock(int mipLevel, pddiRect* rect = 0);
    void Unlock(int mipLevel);

    // paging control
    void Prefetch(void);
    void Discard(void);
    void SetPriority(int priority);
    int GetPriority();

    // gxm specific stuff
    SceGxmTexture* GetGXMTexture(void) { return &texture; }

    // Number of mip levels that currently hold valid data (1 = base level only).
    int GetMipCount(void) const { return validMips; }
    // For the sprite dump: level 0 as it is in memory.
    const void* DebugBits(void) const { return bits; }
    int DebugStride(void) const { return stride; }
    bool DebugSwizzled(void) const { return swizzled; }
    int DebugDepth(void) const { return lock.depth; }
    // A sample of the pixels of level 0, to see whether the memory of a texture changed.
    unsigned PixelSample(void) const
    {
        if(bits == NULL)
            return 0;
        const unsigned* words = (const unsigned*)bits;
        const int count = (int)( ( (size_t)xSize * ySize * ( lock.depth / 8 ) ) / 4 );
        unsigned sum = 0;
        for(int i = 0; i < count; i += 61)
            sum = ( sum * 31 ) + words[ i ];
        return sum;
    }

    // Sets filtering / addressing / mip state on the GXM texture, but only the parts that
    // differ from what was set last time (the five-plus sceGxmTextureSet* calls per material
    // change add up). mipCount 0 leaves the mip state alone (texture without a mip chain).
    void ApplySamplerState(SceGxmTextureFilter mag, SceGxmTextureFilter min,
                           SceGxmTextureAddrMode u, SceGxmTextureAddrMode v,
                           int mipCount, SceGxmTextureMipFilter mipFilter);

protected:
    gxmContext* context;
    unsigned contextID;

    int log2X, log2Y;
    int xSize, ySize;
    pddiTextureType type;
    int nMipMap;

    SceGxmTexture texture;
    int priority;

    // what ApplySamplerState() last wrote into 'texture'; invalid after anything else touches it
    struct SamplerState
    {
        bool valid;
        uint8_t mag, min, u, v;
        uint8_t mipFilter;
        int mipCount;
    } sampler;

    pddiLockInfo lock;

    // Static, power-of-two, uncompressed textures (P8 / RGB565 / 32 bit) are stored swizzled
    // with a full mip chain down to 8 pixels; everything else (DXT keeps its own swizzled
    // base level, dynamic/NPOT/YUV stay linear) only stores the base level. Lock()/Unlock()
    // of levels > 0 go through a temporary buffer that is discarded, since the mip chain is
    // generated from level 0 when it is unlocked.
    uint8_t* bits;
    int stride;         // bytes per row of the base level (linear textures)
    void* lockTemp;     // temporary buffer of the current Lock() (DXT, swizzled, mip > 0), or NULL
    bool swizzled;      // stored swizzled (uncompressed path)
    bool hasAlpha;      // 32 bit texture with an alpha channel
    int mipCount;       // levels allocated
    int validMips;      // levels that hold valid data
    void* pendingLinear;// P8 only: level 0 (tight, memory order) kept until the palette arrives
    float statKB;       // what was added to the texture memory stat

    void UploadChain(const uint8_t* level0, const uint32_t* palette, int paletteEntries, int firstLevel);
    void BuildPaletteMips();

    // native 8-bit palettized storage (P8)
    uint32_t* paletteMem;   // 256 entries, GPU readable
    int nPalette;
    bool opaquePalette;     // texture has no alpha: force palette alpha to 0xff
};
#endif