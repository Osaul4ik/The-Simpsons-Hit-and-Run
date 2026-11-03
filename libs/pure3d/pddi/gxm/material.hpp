//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _MATERIAL_HPP_
#define _MATERIAL_HPP_

#include <pddi/pddi.hpp>
#include <pddi/base/baseshader.hpp>
#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/context.hpp>
class gxmTexture;
class gxmProgram;

const int gxmMaxPasses = 1;

struct gxmTextureEnv
{
    bool enabled;
    gxmTexture* texture;

    int uvSet;
    pddiTextureGen texGen;
    pddiUVMode uvMode;
    pddiFilterMode filterMode;

    bool alphaTest;
    pddiBlendMode alphaBlendMode;
    pddiCompareMode alphaCompareMode;
    float alphaRef;
    int writeMask;

    bool lit;
    bool twoSided;
    pddiShadeMode shadeMode;
    pddiColour diffuse;
    pddiColour specular;
    pddiColour ambient;
    pddiColour emissive;
    float shininess;
};

class gxmMat : public pddiBaseShader
{
public:
    gxmMat(gxmContext*);
    ~gxmMat();

    static pddiShadeColourTable colourTable[];
    static pddiShadeTextureTable textureTable[];
    static pddiShadeIntTable intTable[];
    static pddiShadeFloatTable floatTable[];

    const char* GetType(void);
    int         GetPasses(void);
    void        SetPass(int pass);

    pddiShadeTextureTable* GetTextureTable(void) { return textureTable;}
    pddiShadeIntTable*     GetIntTable(void)     { return intTable;}
    int GetBlendMode(void) const { return texEnv[0].alphaBlendMode; }
    void SetNoMipBlend(bool b) { noMipBlend = b; }  // for what is too soft to show the step between levels
    pddiShadeFloatTable*   GetFloatTable(void)   { return floatTable;}
    pddiShadeColourTable*  GetColourTable(void)  { return colourTable;}

    // texture
    void SetTexture(pddiTexture* texture);
    void SetUVMode(int mode);
    void SetFilterMode(int mode);

    // shading
    void SetShadeMode(int shade);
    void SetTwoSided(int);

    // lighting
    void EnableLighting(int);

    void SetDiffuse(pddiColour colour);
    void SetAmbient(pddiColour colour);
    void SetEmissive(pddiColour);
    void SetEmissiveAlpha(int);
    void SetSpecular(pddiColour);
    void SetShininess(float power);

    // alpha blending
    void SetBlendMode(int mode);
    void EnableAlphaTest(int);
    void SetAlphaCompare(int compare);
    void SetAlphaRef(float ref);

    void SetColourWrite(int mask);

    int  CountDevPasses(void);
    void SetDevPass(unsigned);
    void SelectProgram();
    bool appliedNoSpecular;     // the fragment program in use has no specular input
    void PreRender(void);

private:
    // Picks the fragment program variant for texEnv's own write mask combined with the
    // context-wide colour write mask (see gxmContext::GetColourWriteMask).
    void SelectFragmentShader(int contextWriteMask);

    gxmContext* context;
    int pass;
    bool valid;
    gxmProgram* program;
    SceGxmFragmentProgram* shader;
    int appliedContextWriteMask;    // context mask 'shader' was built for
    gxmTextureEnv texEnv[gxmMaxPasses];
    bool noMipBlend = false;
};

#endif