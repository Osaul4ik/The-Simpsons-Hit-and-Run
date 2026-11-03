//=============================================================================
// See shaderbuild.hpp. Sources mirror shaders/vertex.cg, texture.cg, color.cg, alpha.cg.
//=============================================================================
#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/shaderbuild.hpp>
#include <raddebug.hpp>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <string>

#include <psp2/io/stat.h>
#include <psp2/kernel/threadmgr.h>
#include <vitashark.h>

#define SHADER_CACHE_DIR "ux0:data/simpsons/shadercache"

static std::string VertexSource(bool skin, bool lit, bool specular)
{
    std::string s;
    if(lit)
    {
        s +=
            "typedef struct {\n"
            "    float4 position;\n"
            "    float4 colour;\n"
            "    float4 attenuation;\n"
            "} LightParams;\n"
            "float3 direction(float4 p1, float4 p2) { return normalize(p2.xyz * sign(p1.w) - p1.xyz * sign(p2.w)); }\n"
            "float product(float3 x, float3 y) { return max(dot(x,y), 0.0); }\n";
        if(specular)
            s += "float power(float x, float y) { return y != 0.0 ? pow(x,y) : 1.0; }\n";
    }
    if(skin)
    {
        // three float4 per matrix, see gxmProgram::SetBones. 768 = 256 matrices.
        s +=
            "uniform float4 bones[768];\n"
            "float3 bonePoint(float4 p, int b) { return float3(dot(p, bones[b]), dot(p, bones[b + 1]), dot(p, bones[b + 2])); }\n";
    }
    s +=
        "void main(float3 position      : POSITION,\n";
    if(lit)
        s += "          float3 normal        : NORMAL,\n";
    s +=
        "          float2 texcoord      : TEXCOORD0,\n"
        "          float4 color         : COLOR,\n";
    if(skin)
        s +=
            "          float3 weights       : TEXCOORD1,\n"
            "          float4 indices       : TEXCOORD2,\n";
    s +=
        "          out float4 oPosition : POSITION,\n"
        "          out float2 tc        : TEXCOORD0,\n"
        "          out float4 cpri      : COLOR0,\n";
    if(specular)
        s += "          out float4 csec      : COLOR1,\n";
    s +=
        "          uniform float4x4 projection,\n"
        "          uniform float4x4 modelview,\n";
    if(lit)
        s +=
            "          uniform float4x4 normalmatrix,\n"
            "          uniform float4 acm,\n"
            "          uniform float4 dcm,\n"
            "          uniform float4 ecm,\n"
            "          uniform LightParams lights[4],\n";
    if(specular)
        s += "          uniform float4 scm,\n";
    s +=
        "          uniform float4 acs) {\n";

    if(skin)
    {
        s +=
            "    int b0 = (int)indices.x * 3;\n"
            "    float4 rest = float4(position, 1.0);\n"
            "    float3 skinned = bonePoint(rest, b0) * weights.x\n"
            "                   + bonePoint(rest, (int)indices.y * 3) * weights.y\n"
            "                   + bonePoint(rest, (int)indices.z * 3) * weights.z\n"
            "                   + bonePoint(rest, (int)indices.w * 3) * (1.0 - (weights.x + weights.y + weights.z));\n"
            "    float4 V = mul(float4(skinned, 1.0), modelview);\n";
        if(lit)
            s +=
                "    float3 skinNormal = float3(dot(normal, bones[b0].xyz), dot(normal, bones[b0 + 1].xyz), dot(normal, bones[b0 + 2].xyz));\n"
                "    float3 n = normalize(mul(skinNormal, float3x3(normalmatrix)));\n";
    }
    else
    {
        s += "    float4 V = mul(float4(position, 1.0), modelview);\n";
        if(lit)
            s += "    float3 n = normalize(mul(normal, float3x3(normalmatrix)));\n";
    }

    if(lit)
    {
        s += "    float3 diff = ecm.rgb + acm.rgb * acs.rgb;\n";
        if(specular)
            s += "    float3 spec = float3(0.0);\n";
        s +=
            "    for (int i = 0; i < 4; i++) {\n"
            "        if (lights[i].position.w < 0.0f) continue;\n"
            "        float3 VP = direction(V, lights[i].position);\n"
            "        float3 k = lights[i].attenuation.rgb;\n"
            "        float d = distance(V.xyz, lights[i].position.xyz);\n"
            "        float att = lights[i].position.w != 0.0 ? 1.0 / (k[0] + k[1] * d + k[2] * d * d) : 1.0;\n"
            "        diff += att * product(n,VP) * dcm.rgb * lights[i].colour.rgb;\n";
        if(specular)
            s +=
                "        float f = product(n,VP) != 0.0 ? 1.0 : 0.0;\n"
                "        float3 h = normalize(VP + float3(0.0, 0.0, 1.0));\n"
                "        float srm = lights[i].attenuation.a;\n"
                "        spec += att * f * power(product(n,h),srm) * scm.rgb * lights[i].colour.rgb;\n";
        s +=
            "    }\n"
            "    cpri = color * float4(diff, dcm.a);\n";
        if(specular)
            s += "    csec = float4(spec, 0.0);\n";
    }
    else
    {
        // What the full shader computes for an unlit draw: all material colours are white and
        // every light is off (gxmProgram::SetMaterialColours / ComputeLightUniforms).
        s += "    cpri = color * float4(1.0 + acs.rgb, 1.0);\n";
    }

    s +=
        "    tc = texcoord;\n"
        "    oPosition = mul(V, projection);\n"
        "}\n";
    return s;
}

// Colours are half: enough for 8 bit output and values a little over 1, and cheaper than float.
// The inputs match the outputs of the vertex programs without specular (no vertex program is
// given to the patcher, so they are linked by position).
static const char* const kColorNoSpec =
    "void main(float2 tc : TEXCOORD0,\n"
    "          half4 cpri : COLOR0,\n"
    "          out half4 color : COLOR) {\n"
    "    color = cpri;\n"
    "}\n";

static const char* const kTextureNoSpec =
    "void main(float2 tc : TEXCOORD0,\n"
    "          half4 cpri : COLOR0,\n"
    "          out half4 color : COLOR,\n"
    "          uniform sampler2D tex) {\n"
    "    half4 t = tex2D(tex, tc);\n"
    "    color = t * cpri;\n"
    "}\n";

static const char* const kAlphaNoSpec =
    "void main(float2 tc : TEXCOORD0,\n"
    "          half4 cpri : COLOR0,\n"
    "          out half4 color : COLOR,\n"
    "          uniform float alpharef,\n"
    "          uniform sampler2D tex) {\n"
    "    half4 t = tex2D(tex, tc);\n"
    "    half4 c = t * cpri;\n"
    "    if (c.a < alpharef) discard;\n"
    "    color = c;\n"
    "}\n";

static std::string SourceOf(gxmShaderId id)
{
    switch(id)
    {
    case GXM_SHADER_V_DIFFUSE:      return VertexSource(false, true, false);
    case GXM_SHADER_V_UNLIT:        return VertexSource(false, false, false);
    case GXM_SHADER_V_SKIN_FULL:    return VertexSource(true, true, true);
    case GXM_SHADER_V_SKIN_DIFFUSE: return VertexSource(true, true, false);
    case GXM_SHADER_V_SKIN_UNLIT:   return VertexSource(true, false, false);
    case GXM_SHADER_F_COLOR_NOSPEC: return kColorNoSpec;
    case GXM_SHADER_F_TEXTURE_NOSPEC: return kTextureNoSpec;
    case GXM_SHADER_F_ALPHA_NOSPEC: return kAlphaNoSpec;
    default: return "";
    }
}

static const char* const kNames[GXM_SHADER_COUNT] =
{
    "v_diffuse", "v_unlit", "v_skin_full", "v_skin_diffuse", "v_skin_unlit",
    "f_color_nospec", "f_texture_nospec", "f_alpha_nospec"
};

static void* LoadFile(const char* path, unsigned* size)
{
    FILE* f = fopen(path, "rb");
    if(f == NULL)
        return NULL;
    fseek(f, 0, SEEK_END);
    const long length = ftell(f);
    fseek(f, 0, SEEK_SET);
    void* data = length > 0 ? malloc(length) : NULL;
    const bool read = data && fread(data, 1, length, f) == (size_t)length;
    fclose(f);
    // an interrupted write or a damaged file counts as not compiled
    if(!read || sceGxmProgramCheck((const SceGxmProgram*)data) != SCE_OK ||
       sceGxmProgramGetSize((const SceGxmProgram*)data) != (unsigned)length)
    {
        free(data);
        return NULL;
    }
    *size = (unsigned)length;
    return data;
}

void* gxmShaderLoad(gxmShaderId id, unsigned* size)
{
    char path[128];
    snprintf(path, sizeof(path), "app0:/shaders/%s.gxp", kNames[id]);
    void* data = LoadFile(path, size);
    if(data)
        return data;
    snprintf(path, sizeof(path), SHADER_CACHE_DIR "/%s.gxp", kNames[id]);
    return LoadFile(path, size);
}

void gxmShaderBuildFree(void* gxp)
{
    free(gxp);
}

bool gxmShaderIsCompiled(gxmShaderId id)
{
    unsigned size = 0;
    void* data = gxmShaderLoad(id, &size);
    free(data);
    return data != NULL;
}

static void CompilerLog(const char* msg, shark_log_level level, int line)
{
    if(level != SHARK_LOG_INFO)
        rReleasePrintf("[shader] line %d: %s\n", line, msg);
}

static bool s_compilerLoaded = false;
static bool s_compilerMissing = false;

static bool LoadCompiler()
{
    if(s_compilerLoaded)
        return true;
    // the two places the installers put it
    if(shark_init(NULL) < 0 && shark_init("ur0:/data/external/libshacccg.suprx") < 0)
    {
        rReleasePrintf("[shader] libshacccg.suprx not found\n");
        s_compilerMissing = true;
        return false;
    }
    shark_install_log_cb(CompilerLog);
    s_compilerLoaded = true;
    s_compilerMissing = false;
    return true;
}

bool gxmShaderCompilerMissing()
{
    return s_compilerMissing;
}

// The compiler recurses deeply: the game thread's 256 KB stack is too little for it.
#define SHADER_COMPILE_STACK_BYTES ( 4 * 1024 * 1024 )

struct CompileJob
{
    gxmShaderId id;
    bool saved;
};

static bool CompileAndSave(gxmShaderId id)
{
    const std::string source = SourceOf(id);
    const bool vertex = id <= GXM_SHADER_V_SKIN_UNLIT;
    // in: length of the source, out: size of the program
    uint32_t compiledSize = (uint32_t)source.size();
    SceGxmProgram* compiled = shark_compile_shader_extended(source.c_str(), &compiledSize,
        vertex ? SHARK_VERTEX_SHADER : SHARK_FRAGMENT_SHADER, SHARK_OPT_FAST, SHARK_ENABLE, SHARK_DISABLE, SHARK_DISABLE);
    bool saved = false;
    if(compiled != NULL)
    {
        char path[128];
        snprintf(path, sizeof(path), SHADER_CACHE_DIR "/%s.gxp", kNames[id]);
        sceIoMkdir(SHADER_CACHE_DIR, 0777);
        FILE* f = fopen(path, "wb");
        if(f)
        {
            saved = fwrite(compiled, 1, compiledSize, f) == compiledSize;
            saved = (fclose(f) == 0) && saved;
        }
        rReleasePrintf("[shader] %s: %u bytes, %s\n", kNames[id], compiledSize, saved ? "saved" : "could not be saved");
    }
    else
    {
        rReleasePrintf("[shader] %s did not compile\n", kNames[id]);
    }
    shark_clear_output();
    return saved;
}

static int CompileThread(SceSize args, void* argp)
{
    CompileJob* job = *(CompileJob**)argp;
    job->saved = CompileAndSave(job->id);
    return sceKernelExitDeleteThread(0);
}

bool gxmShaderCompile(gxmShaderId id)
{
    if(!LoadCompiler())
        return false;

    CompileJob job = { id, false };
    CompileJob* jobPointer = &job;
    const SceUID thread = sceKernelCreateThread("shader_compile", CompileThread, 0x10000100, SHADER_COMPILE_STACK_BYTES, 0, 0, NULL);
    if(thread < 0)
    {
        rReleasePrintf("[shader] could not create the compile thread: 0x%08x\n", thread);
        return false;
    }
    sceKernelStartThread(thread, sizeof(jobPointer), &jobPointer);
    sceKernelWaitThreadEnd(thread, NULL, NULL);
    return job.saved;
}

static bool s_variantsInUse = false;
static bool s_skinningInUse = false;

void gxmShaderSetInUse(bool variants, bool hardwareSkinning)
{
    s_variantsInUse = variants;
    s_skinningInUse = hardwareSkinning;
}

bool gxmShaderVariantsInUse()
{
    return s_variantsInUse;
}

bool gxmShaderHardwareSkinningInUse()
{
    return s_skinningInUse;
}
