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

protected:
    gxmContext* context;
    unsigned contextID;

    int log2X, log2Y;
    int xSize, ySize;
    pddiTextureType type;
    int nMipMap;

    SceGxmTexture texture;
    int priority;

    pddiLockInfo lock;

    // Only the base level is stored on the GPU (the GXM backend never samples mip levels).
    // Lock()/Unlock() of higher levels go through a temporary buffer that is discarded.
    uint8_t* bits;
    int stride;         // bytes per row of the base level (from sceGxmTextureGetStride)
    void* lockTemp;     // temporary buffer of the current Lock() (DXT and mip > 0), or NULL

    // native 8-bit palettized storage (P8)
    uint32_t* paletteMem;   // 256 entries, GPU readable
    int nPalette;
    bool opaquePalette;     // texture has no alpha: force palette alpha to 0xff
};
#endif