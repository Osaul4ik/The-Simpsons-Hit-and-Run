//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <string.h>
#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/program.hpp>
#include <pddi/gxm/material.hpp>
#include <p3d/file.hpp>
#include <radmemory.hpp>

#include <string>
#include <vector>
#include <SDL.h>

static inline void UniformColour(void* buffer, const SceGxmProgramParameter* param, pddiColour c)
{
    float colour[] = { float(c.Red()) / 255, float(c.Green()) / 255, float(c.Blue()) / 255, float(c.Alpha()) / 255 };
    sceGxmSetUniformDataF(buffer, param, 0, 4, colour);
}

gxmProgram::gxmProgram(SceGxmShaderPatcher* patcher, tFile* gxp)
    : program()
    , shaderPatcher(patcher)
    , shaderCache()
{
    gxp->AddRef();
    program = (SceGxmProgram*)radMemoryAlloc(radMemoryGetCurrentAllocator(), gxp->GetSize());
    gxp->GetData(program, gxp->GetSize());
    gxp->Release();
    CHK_GXM(sceGxmProgramCheck(program));
    type = sceGxmProgramGetType(program);

    projection = sceGxmProgramFindParameterByName(program, "projection");
    modelview = sceGxmProgramFindParameterByName(program, "modelview");
    normalmatrix = sceGxmProgramFindParameterByName(program, "normalmatrix");
    alpharef = sceGxmProgramFindParameterByName(program, "alpharef");
    sampler = sceGxmProgramFindParameterByName(program, "tex");

    for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        std::string prefix = std::string("lights[") + char('0' + i) + "].";
        lights[i].position = sceGxmProgramFindParameterByName(program, (prefix + "position").c_str());
        lights[i].colour = sceGxmProgramFindParameterByName(program, (prefix + "colour").c_str());
        lights[i].attenuation = sceGxmProgramFindParameterByName(program, (prefix + "attenuation").c_str());
    }

    acs = sceGxmProgramFindParameterByName(program, "acs");
    acm = sceGxmProgramFindParameterByName(program, "acm");
    dcm = sceGxmProgramFindParameterByName(program, "dcm");
    scm = sceGxmProgramFindParameterByName(program, "scm");
    ecm = sceGxmProgramFindParameterByName(program, "ecm");

    position = sceGxmProgramFindParameterByName(program, "position");
    normal = sceGxmProgramFindParameterByName(program, "normal");
    texcoord = sceGxmProgramFindParameterByName(program, "texcoord");
    color = sceGxmProgramFindParameterByName(program, "color");

    CHK_GXM(sceGxmShaderPatcherRegisterProgram(patcher, program, &patcherId));
}

gxmProgram::~gxmProgram()
{
    for(std::pair<unsigned, void*> shader : shaderCache)
    {
        switch(type)
        {
        case SCE_GXM_VERTEX_PROGRAM:
            sceGxmShaderPatcherReleaseVertexProgram(shaderPatcher,
                reinterpret_cast<SceGxmVertexProgram*>(shader.second));
            break;
        case SCE_GXM_FRAGMENT_PROGRAM:
            sceGxmShaderPatcherReleaseFragmentProgram(shaderPatcher,
                reinterpret_cast<SceGxmFragmentProgram*>(shader.second));
            break;
        default:
            PDDIASSERT(false);
            break;
        }
    }
    radMemoryFree(program);
}

void gxmProgram::SetProjectionMatrix(void* buffer, const pddiMatrix* matrix)
{
    if(projection)
        sceGxmSetUniformDataF(buffer, projection, 0, 16, matrix->m[0]);
}

void gxmProgram::SetModelViewMatrix(void* buffer, const pddiMatrix* matrix, bool needNormalMatrix)
{
    if(modelview != NULL)
        sceGxmSetUniformDataF(buffer, modelview, 0, 16, matrix->m[0]);

    // The normal matrix is only read by the lighting loop. Unlit draws set every light to
    // "disabled" (position.w < 0), so the shader never touches it and the 4x4 inverse can be skipped.
    if(normalmatrix != NULL && needNormalMatrix)
    {
        // Consecutive draws very often use the same modelview (several materials per object,
        // multi-pass), so remember the last inverse-transpose instead of recomputing it.
        static pddiMatrix cachedSource;
        static pddiMatrix cachedNormal;
        static bool cacheValid = false;

        if(!cacheValid || memcmp(&cachedSource, matrix, sizeof(pddiMatrix)) != 0)
        {
            cachedSource = *matrix;
            cachedNormal.Invert(*matrix);
            cachedNormal.Transpose();
            cacheValid = true;
        }
        sceGxmSetUniformDataF(buffer, normalmatrix, 0, 16, cachedNormal.m[0]);
    }
}

void gxmProgram::SetTextureEnvironment(void* buffer, const gxmTextureEnv* texEnv)
{
    if(texEnv && texEnv->lit)
    {
        UniformColour(buffer, acm, texEnv->ambient);
        UniformColour(buffer, ecm, texEnv->emissive);
        UniformColour(buffer, dcm, texEnv->diffuse);
        UniformColour(buffer, scm, texEnv->specular);
    }
    else
    {
        UniformColour(buffer, acm, pddiColour(-1));
        UniformColour(buffer, ecm, pddiColour(-1));
        UniformColour(buffer, dcm, pddiColour(-1));
        UniformColour(buffer, scm, pddiColour(-1));
    }
}

void gxmProgram::SetAlphaTest(void* buffer, const gxmTextureEnv* texEnv)
{
    if(texEnv->alphaTest && alpharef != NULL)
    {
        PDDIASSERT(texEnv->alphaCompareMode == PDDI_COMPARE_GREATER ||
            texEnv->alphaCompareMode == PDDI_COMPARE_GREATEREQUAL);
        float ref = texEnv->alphaTest ? texEnv->alphaRef : 0.0f;
        sceGxmSetUniformDataF(buffer, alpharef, 0, 1, &ref);
    }
}

void gxmProgram::ComputeLightUniforms( const pddiLight* lightState, const pddiVector* viewPosition, const pddiVector* viewDirection, bool lit, float shininess, float dir[4], float attenuation[4] )
{
    dir[0] = 0.0f; dir[1] = 0.0f; dir[2] = 0.0f; dir[3] = -1.0f;
    if (lightState->enabled && lit)
    {
        switch(lightState->type)
        {
            case PDDI_LIGHT_DIRECTIONAL :
                dir[0] = -viewDirection->x;
                dir[1] = -viewDirection->y;
                dir[2] = -viewDirection->z;
                dir[3] = 0.0f;
                break;

            case PDDI_LIGHT_POINT :
                dir[0] = viewPosition->x;
                dir[1] = viewPosition->y;
                dir[2] = viewPosition->z;
                dir[3] = 1.0f;
                break;

            case PDDI_LIGHT_SPOT :
                PDDIASSERT(0);
                break;
        }
    }

    attenuation[0] = lightState->attenA;
    attenuation[1] = lightState->attenB;
    attenuation[2] = lightState->attenC;
    attenuation[3] = shininess;
}

void gxmProgram::SetLightValues( void* buffer, int handle, const float dir[4], pddiColour colour, const float attenuation[4] )
{
    if(handle >= PDDI_MAX_LIGHTS)
        return;

    sceGxmSetUniformDataF(buffer, lights[handle].position, 0, 4, dir);

    // A disabled light (position.w < 0) is skipped by the shader before its colour and
    // attenuation are read, so those uniforms are only written for lights that contribute.
    if(dir[3] < 0.0f)
        return;

    UniformColour(buffer, lights[handle].colour, colour);
    sceGxmSetUniformDataF(buffer, lights[handle].attenuation, 0, 4, attenuation);
}

void gxmProgram::SetLightState( void* buffer, int handle, const pddiLight* lightState, const pddiVector* viewPosition, const pddiVector* viewDirection, bool lit, float shininess )
{
    if(handle >= PDDI_MAX_LIGHTS)
        return;

    float dir[4], attenuation[4];
    ComputeLightUniforms(lightState, viewPosition, viewDirection, lit, shininess, dir, attenuation);
    SetLightValues(buffer, handle, dir, lightState->colour, attenuation);
}

void gxmProgram::SetMaterialColours( void* buffer, pddiColour ambient, pddiColour emissive, pddiColour diffuse, pddiColour specular )
{
    UniformColour(buffer, acm, ambient);
    UniformColour(buffer, ecm, emissive);
    UniformColour(buffer, dcm, diffuse);
    UniformColour(buffer, scm, specular);
}

void gxmProgram::SetAmbientLight(void* buffer, pddiColour ambient)
{
    if(acs)
        UniformColour(buffer, acs, ambient);
}


SceGxmVertexProgram* gxmProgram::PatchVertexShader(unsigned int vertexType, uint16_t stride)
{
    SceGxmVertexProgram* vert = nullptr;
    uint16_t offset = 0;
    int attr = 0, strm = 0;

    PDDIASSERT(type == SCE_GXM_VERTEX_PROGRAM);
    vert = reinterpret_cast<SceGxmVertexProgram*>(shaderCache[vertexType]);
    if(vert)
        return vert;

    SceGxmVertexAttribute vertexAttributes[4];
    SceGxmVertexStream vertexStreams[2];
    vertexAttributes[attr].streamIndex = 0;
    vertexAttributes[attr].offset = offset;
    vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    vertexAttributes[attr].componentCount = 3;
    vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(position);
    attr++;
    offset += 12;

    if(vertexType & PDDI_V_NORMAL)
    {
        vertexAttributes[attr].streamIndex = 0;
        vertexAttributes[attr].offset = offset;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 3;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(normal);
        attr++;
        offset += 12;
    }
    else
    {
        vertexAttributes[attr].streamIndex = 1;
        vertexAttributes[attr].offset = 0;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 3;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(normal);
        attr++;
    }

    if(vertexType & 0xf)
    {
        vertexAttributes[attr].streamIndex = 0;
        vertexAttributes[attr].offset = offset;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 2;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(texcoord);
        attr++;
        offset += 8;
    }
    else
    {
        vertexAttributes[attr].streamIndex = 1;
        vertexAttributes[attr].offset = 0;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 2;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(texcoord);
        attr++;
    }

    if(vertexType & PDDI_V_COLOUR)
    {
        vertexAttributes[attr].streamIndex = 0;
        vertexAttributes[attr].offset = offset;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_U8N;
        vertexAttributes[attr].componentCount = 4;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(color);
        attr++;
        offset += 4;
    }
    else
    {
        vertexAttributes[attr].streamIndex = 1;
        vertexAttributes[attr].offset = 0;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 4;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(color);
        attr++;
    }

    vertexStreams[strm].stride = stride;
    vertexStreams[strm].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    strm++;
    vertexStreams[strm].stride = 0;
    vertexStreams[strm].indexSource = SCE_GXM_INDEX_SOURCE_INDEX_16BIT;
    strm++;

    CHK_GXM(sceGxmShaderPatcherCreateVertexProgram(
        shaderPatcher,
        patcherId,
        vertexAttributes,
        attr,
        vertexStreams,
        strm,
        &vert));
    return vert;
}

SceGxmFragmentProgram* gxmProgram::PatchFragmentShader(const SceGxmBlendInfo* blendInfo, SceGxmMultisampleMode msaaMode)
{
    SceGxmFragmentProgram* frag = nullptr;
    PDDIASSERT(type == SCE_GXM_FRAGMENT_PROGRAM);

    unsigned int key = blendInfo ? *reinterpret_cast<const unsigned int*>(blendInfo) : 0;
    frag = reinterpret_cast<SceGxmFragmentProgram*>(shaderCache[key]);
    if(frag)
        return frag;

    CHK_GXM(sceGxmShaderPatcherCreateFragmentProgram(
        shaderPatcher,
        patcherId,
        SCE_GXM_OUTPUT_REGISTER_FORMAT_UCHAR4,
        msaaMode,
        blendInfo,
        NULL,
        &frag));
    return frag;
}