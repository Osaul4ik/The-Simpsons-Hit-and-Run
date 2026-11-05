//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <string.h>
#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/program.hpp>
extern SceGxmFragmentProgram* g_gxmBoundFragment;   // context.cpp
#include <pddi/gxm/material.hpp>
#include <p3d/file.hpp>
#include <radmemory.hpp>
#include <arm_neon.h>

#include <string>
#include <vector>
#include <SDL.h>

// sceGxmSetUniformDataF is an imported function that looks at the parameter's category and type
// for every call, and a lit draw makes about 20 of them. A float parameter's data simply lives
// resourceIndex dwords into the default uniform buffer, so write it directly (parameters of any
// other type or buffer, which the shaders here do not have, still go through GXM).
enum { DEFAULT_UNIFORM_CONTAINER = 14 };   // SCE_GXM_DEFAULT_UNIFORM_BUFFER_CONTAINER_INDEX, not in the vitasdk headers

static int FastUniformOffset(const SceGxmProgramParameter* param)
{
    if(param == NULL ||
       sceGxmProgramParameterGetCategory(param) != SCE_GXM_PARAMETER_CATEGORY_UNIFORM ||
       sceGxmProgramParameterGetType(param) != SCE_GXM_PARAMETER_TYPE_F32 ||
       sceGxmProgramParameterGetContainerIndex(param) != DEFAULT_UNIFORM_CONTAINER)
        return -1;
    return (int)sceGxmProgramParameterGetResourceIndex(param) * 4;
}

static inline void UniformFloats(void* buffer, const SceGxmProgramParameter* param, int offset, unsigned count, const float* data)
{
    if(offset >= 0)
        memcpy((char*)buffer + offset, data, count * sizeof(float));
    else if(param)
        sceGxmSetUniformDataF(buffer, param, 0, count, data);
}

static inline void UniformColour(void* buffer, const SceGxmProgramParameter* param, int offset, pddiColour c)
{
    const float scale = 1.0f / 255.0f;
    const float colour[] = { c.Red() * scale, c.Green() * scale, c.Blue() * scale, c.Alpha() * scale };
    UniformFloats(buffer, param, offset, 4, colour);
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
    Init(patcher);
}

gxmProgram::gxmProgram(SceGxmShaderPatcher* patcher, const void* gxp, unsigned size)
    : program()
    , shaderPatcher(patcher)
    , shaderCache()
{
    program = (SceGxmProgram*)radMemoryAlloc(radMemoryGetCurrentAllocator(), size);
    memcpy(program, gxp, size);
    Init(patcher);
}

void gxmProgram::Init(SceGxmShaderPatcher* patcher)
{
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
    weights = sceGxmProgramFindParameterByName(program, "weights");
    indices = sceGxmProgramFindParameterByName(program, "indices");

    bonesOffset = FastUniformOffset(sceGxmProgramFindParameterByName(program, "bones"));
    projectionOffset = FastUniformOffset(projection);
    modelviewOffset = FastUniformOffset(modelview);
    normalmatrixOffset = FastUniformOffset(normalmatrix);
    acsOffset = FastUniformOffset(acs);
    acmOffset = FastUniformOffset(acm);
    dcmOffset = FastUniformOffset(dcm);
    scmOffset = FastUniformOffset(scm);
    ecmOffset = FastUniformOffset(ecm);
    for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        lightOffsets[i].position = FastUniformOffset(lights[i].position);
        lightOffsets[i].colour = FastUniformOffset(lights[i].colour);
        lightOffsets[i].attenuation = FastUniformOffset(lights[i].attenuation);
    }

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
            if(g_gxmBoundFragment == reinterpret_cast<SceGxmFragmentProgram*>(shader.second))
                g_gxmBoundFragment = NULL;
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

// The inverse transpose of the rotation and scale part, up to a factor: the shader normalizes the
// result and only reads these three rows. So no division and no full 4x4 inverse for every object.
// The cofactors keep the direction for any scale; the sign of the determinant keeps it for a mirror.
static void ComputeNormalMatrix(const pddiMatrix& m, pddiMatrix& n)
{
    const float a0 = m.m[0][0], a1 = m.m[0][1], a2 = m.m[0][2];
    const float b0 = m.m[1][0], b1 = m.m[1][1], b2 = m.m[1][2];
    const float c0 = m.m[2][0], c1 = m.m[2][1], c2 = m.m[2][2];
    const float n00 = b1 * c2 - b2 * c1, n01 = b2 * c0 - b0 * c2, n02 = b0 * c1 - b1 * c0;
    const float sign = (a0 * n00 + a1 * n01 + a2 * n02) < 0.0f ? -1.0f : 1.0f;
    n.m[0][0] = sign * n00;
    n.m[0][1] = sign * n01;
    n.m[0][2] = sign * n02;
    n.m[0][3] = 0.0f;
    n.m[1][0] = sign * (c1 * a2 - c2 * a1);
    n.m[1][1] = sign * (c2 * a0 - c0 * a2);
    n.m[1][2] = sign * (c0 * a1 - c1 * a0);
    n.m[1][3] = 0.0f;
    n.m[2][0] = sign * (a1 * b2 - a2 * b1);
    n.m[2][1] = sign * (a2 * b0 - a0 * b2);
    n.m[2][2] = sign * (a0 * b1 - a1 * b0);
    n.m[2][3] = 0.0f;
    n.m[3][0] = 0.0f;
    n.m[3][1] = 0.0f;
    n.m[3][2] = 0.0f;
    n.m[3][3] = 1.0f;
}

void gxmProgram::SetProjectionMatrix(void* buffer, const pddiMatrix* matrix)
{
    UniformFloats(buffer, projection, projectionOffset, 16, matrix->m[0]);
}

void gxmProgram::SetModelViewMatrix(void* buffer, const pddiMatrix* matrix, bool needNormalMatrix)
{
    UniformFloats(buffer, modelview, modelviewOffset, 16, matrix->m[0]);

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
            ComputeNormalMatrix(*matrix, cachedNormal);
            cacheValid = true;
        }
        UniformFloats(buffer, normalmatrix, normalmatrixOffset, 16, cachedNormal.m[0]);
    }
}

void gxmProgram::SetTextureEnvironment(void* buffer, const gxmTextureEnv* texEnv)
{
    if(texEnv && texEnv->lit)
    {
        UniformColour(buffer, acm, acmOffset, texEnv->ambient);
        UniformColour(buffer, ecm, ecmOffset, texEnv->emissive);
        UniformColour(buffer, dcm, dcmOffset, texEnv->diffuse);
        UniformColour(buffer, scm, scmOffset, texEnv->specular);
    }
    else
    {
        UniformColour(buffer, acm, acmOffset, pddiColour(-1));
        UniformColour(buffer, ecm, ecmOffset, pddiColour(-1));
        UniformColour(buffer, dcm, dcmOffset, pddiColour(-1));
        UniformColour(buffer, scm, scmOffset, pddiColour(-1));
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

    UniformFloats(buffer, lights[handle].position, lightOffsets[handle].position, 4, dir);

    // A disabled light (position.w < 0) is skipped by the shader before its colour and
    // attenuation are read, so those uniforms are only written for lights that contribute.
    if(dir[3] < 0.0f)
        return;

    UniformColour(buffer, lights[handle].colour, lightOffsets[handle].colour, colour);
    UniformFloats(buffer, lights[handle].attenuation, lightOffsets[handle].attenuation, 4, attenuation);
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
    UniformColour(buffer, acm, acmOffset, ambient);
    UniformColour(buffer, ecm, ecmOffset, emissive);
    UniformColour(buffer, dcm, dcmOffset, diffuse);
    UniformColour(buffer, scm, scmOffset, specular);
}

void gxmProgram::SetBones(void* buffer, const pddiMatrix* const* palette, unsigned count)
{
    if(bonesOffset < 0)
        return;
    // Column i of the (row vector) matrix goes into a float4, so the shader computes component i
    // of the transformed position with dot(float4(p, 1), column). The last column (0,0,0,1) is
    // not needed.
    float* out = (float*)((char*)buffer + bonesOffset);
    for(unsigned i = 0; i < count; i++)
    {
        const float32x4x4_t m = vld4q_f32(palette[i]->m[0]);
        vst1q_f32(out, m.val[0]);
        vst1q_f32(out + 4, m.val[1]);
        vst1q_f32(out + 8, m.val[2]);
        out += 12;
    }
}

void gxmProgram::SetAmbientLight(void* buffer, pddiColour ambient)
{
    UniformColour(buffer, acs, acsOffset, ambient);
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

    SceGxmVertexAttribute vertexAttributes[6];
    SceGxmVertexStream vertexStreams[2];
    vertexAttributes[attr].streamIndex = 0;
    vertexAttributes[attr].offset = offset;
    vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
    vertexAttributes[attr].componentCount = 3;
    vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(position);
    attr++;
    offset += 12;

    // Shader variants do not read everything (the unlit one has no normal). The vertex layout
    // stays the same, a parameter the program does not have just gets no attribute.
    if(vertexType & PDDI_V_NORMAL)
    {
        if(normal)
        {
            vertexAttributes[attr].streamIndex = 0;
            vertexAttributes[attr].offset = offset;
            vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
            vertexAttributes[attr].componentCount = 3;
            vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(normal);
            attr++;
        }
        offset += 12;
    }
    else if(normal)
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

    // skinned buffers only (gxmPrimBuffer: weights and indices follow the other attributes)
    if(weights && indices)
    {
        vertexAttributes[attr].streamIndex = 0;
        vertexAttributes[attr].offset = offset;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_F32;
        vertexAttributes[attr].componentCount = 3;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(weights);
        attr++;
        offset += 12;
        vertexAttributes[attr].streamIndex = 0;
        vertexAttributes[attr].offset = offset;
        vertexAttributes[attr].format = SCE_GXM_ATTRIBUTE_FORMAT_U8;
        vertexAttributes[attr].componentCount = 4;
        vertexAttributes[attr].regIndex = sceGxmProgramParameterGetResourceIndex(indices);
        attr++;
        offset += 4;
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