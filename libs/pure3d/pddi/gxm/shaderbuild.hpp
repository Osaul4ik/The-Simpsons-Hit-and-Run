//=============================================================================
// Shader variants compiled on the console with libshacccg, on request from the Options
// menu, and saved to ux0:data/simpsons/shadercache under their final names. Copy them to
// libs/pure3d/pddi/gxm/shaders and they go into the VPK. This needs no Sony SDK, and
// without them the game uses the original .gxp files.
//=============================================================================
#ifndef _GXM_SHADERBUILD_HPP_
#define _GXM_SHADERBUILD_HPP_

enum gxmShaderId
{
    GXM_SHADER_V_DIFFUSE,
    GXM_SHADER_V_UNLIT,
    GXM_SHADER_V_SKIN_FULL,
    GXM_SHADER_V_SKIN_DIFFUSE,
    GXM_SHADER_V_SKIN_UNLIT,
    GXM_SHADER_F_COLOR_NOSPEC,
    GXM_SHADER_F_TEXTURE_NOSPEC,
    GXM_SHADER_F_ALPHA_NOSPEC,
    GXM_SHADER_COUNT
};

// Loads a compiled program (free it with gxmShaderBuildFree), from the VPK or the memory card.
// Never compiles: that only happens when the player asks for it in the Options menu.
void* gxmShaderLoad(gxmShaderId id, unsigned* size);
void gxmShaderBuildFree(void* gxp);

// For the Options menu
bool gxmShaderIsCompiled(gxmShaderId id);
bool gxmShaderCompile(gxmShaderId id);      // compiles and saves one program; slow, returns when done
bool gxmShaderCompilerMissing();            // libshacccg.suprx could not be loaded

// What the running game uses; programs are only picked up when the game starts
void gxmShaderSetInUse(bool variants, bool hardwareSkinning);
bool gxmShaderVariantsInUse();
bool gxmShaderHardwareSkinningInUse();

#endif
