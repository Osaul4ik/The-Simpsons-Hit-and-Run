//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/context.hpp>
#include <pddi/gxm/device.hpp>
#include <pddi/gxm/display.hpp>
#include <pddi/gxm/texture.hpp>
#include <pddi/gxm/material.hpp>
#include <pddi/gxm/program.hpp>

#include <pddi/base/debug.hpp>
#include <p3d/utility.hpp>
#include <radmemory.hpp>
#ifdef RAD_LOG
#include <raddebug.hpp>
// Defined in code/main/win32main.cpp (logs a milestone plus kernel/newlib/heap memory).
// Weak so pure3d still links without the game executable's code.
void LogProgress( const char* stage ) __attribute__((weak));
#endif

#include <math.h>
#include <string.h>
#include <SDL.h>
#include <vector>

#include <pddi/gxm/shaderbuild.hpp>
#include <p3d/vitajobs.hpp>
#include <pddi/pddiext.hpp>

int g_gxmFadeAlpha = 255;    // what is being drawn now fades in, see WorldScene::RenderTranslucent

// Time in the draw calls in the log (code/debug/vitaprof.cpp). This is the part of rendering a
// thread of its own could take over, without the game code above it having to change.
#ifdef RAD_LOG
#include <psp2/kernel/processmgr.h>
extern "C" void vitaProfAdd(const char* name, unsigned microseconds);
extern int g_vitaProfEnabled;
namespace
{
    struct GxmDrawTimer
    {
        const char* name;
        SceUInt32 start;
        GxmDrawTimer(const char* n) : name(n), start(g_vitaProfEnabled ? sceKernelGetProcessTimeLow() : 0) {}
        ~GxmDrawTimer()
        {
            if(g_vitaProfEnabled)
                vitaProfAdd(name, sceKernelGetProcessTimeLow() - start);
        }
    };
}
#define GXM_DRAW_TIMER() GxmDrawTimer gxmDrawTimer("GXM: draw calls")
#define GXM_NAMED_TIMER(name) GxmDrawTimer gxmNamedTimer(name)
#else
#define GXM_DRAW_TIMER()
#define GXM_NAMED_TIMER(name)
#endif

// Runtime switches, from vita.cfg ("fastshaders=0", "hwskin=0"; code/main/win32main.cpp)
extern int g_gxmFastShaders;
extern int g_gxmHardwareSkinning;

class gxmHardwareSkinning : public pddiExtHardwareSkinning
{
public:
    enum { MAX_PALETTE = 256 };     // vertex indices are bytes; also the size of "bones" in the shader

    gxmHardwareSkinning(gxmContext* c) : context(c), count(0) {}

    int MaxMatrixPaletteSize(unsigned) { return MAX_PALETTE; }
    void Begin(void) {}
    void End(void) {}
    void SetMatrixCount(unsigned n) { count = n > MAX_PALETTE ? MAX_PALETTE : n; }
    void SetMatrix(unsigned index, pddiMatrix* m)
    {
        if(index < MAX_PALETTE)
            palette[index] = m;
    }
    void DrawSkin(pddiShader* shader, pddiPrimBuffer* skin) { context->DrawSkinned(shader, skin, palette, count); }

private:
    gxmContext* context;
    unsigned count;
    const pddiMatrix* palette[MAX_PALETTE];
};

// Reuse the bound vertex default uniform buffer for consecutive draws whose matrices, lights and
// material colours did not change, instead of reserving and filling a new one for every draw.
// This relies on the default uniform buffer staying bound across draws made with the same vertex
// program (the shadow is dropped on every vertex program change, mid-scene flush and new scene).
// On by default. If geometry looks wrong or flickers on hardware, build with
// -DRAD_GXM_UNIFORM_REUSE=0 to go back to one fresh buffer per draw. The periodic RAD_LOG
// line shows how many uploads were saved.
#ifndef RAD_GXM_UNIFORM_REUSE
#define RAD_GXM_UNIFORM_REUSE 1
#endif

// vertex arrays rendering
SceGxmPrimitiveType primTypeTable[5] =
{
    SCE_GXM_PRIMITIVE_TRIANGLES,      // PDDI_PRIM_TRIANGLES
    SCE_GXM_PRIMITIVE_TRIANGLE_STRIP, // PDDI_PRIM_TRISTRIP
    SCE_GXM_PRIMITIVE_LINES,          // PDDI_PRIM_LINES
    SCE_GXM_PRIMITIVE_LINES,          // PDDI_PRIM_LINESTRIP
    SCE_GXM_PRIMITIVE_POINTS,         // PDDI_PRIM_POINTS
};

SceGxmPolygonMode polyModeTable[5] =
{
    SCE_GXM_POLYGON_MODE_TRIANGLE_FILL, // PDDI_PRIM_TRIANGLES
    SCE_GXM_POLYGON_MODE_TRIANGLE_FILL, // PDDI_PRIM_TRISTRIP
    SCE_GXM_POLYGON_MODE_LINE,          // PDDI_PRIM_LINES
    SCE_GXM_POLYGON_MODE_LINE,          // PDDI_PRIM_LINESTRIP
    SCE_GXM_POLYGON_MODE_POINT,         // PDDI_PRIM_POINTS
};

class gxmExtGamma : public pddiExtGammaControl
{
public:
    gxmExtGamma(gxmDisplay* d) { display = d;}

    void SetGamma(float r, float g, float b)     {display->SetGamma(r,g,b);}
    void GetGamma(float *r, float *g, float *b)  {display->GetGamma(r,g,b);}

protected:
    gxmDisplay* display;
};

void* gxmContext::patcherHostAlloc( void* userData, uint32_t size )
{
    return malloc( size );
}

void gxmContext::patcherHostFree( void* userData, void* mem )
{
    free( mem );
}

gxmContext::gxmContext(gxmDevice* dev, gxmDisplay* disp) : pddiBaseContext((pddiDisplay*)disp, (pddiDevice*)dev)
{
    device = dev;
    display = disp;
    context = display->GetGXMContext();
    vertexShader = nullptr;
    uniformShadow.valid = false;
    uniformUploads = uniformReuses = 0;
    // DefaultState() below syncs the scissor and stencil state, which look at these
    inScene = false;
    for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        lightViewPosition[i].Set(0.0f, 0.0f, 0.0f);
        lightViewDirection[i].Set(0.0f, 0.0f, -1.0f);
    }
    stencilDirty = true;
    stencilHwMode = STENCIL_HW_UNKNOWN;

    device->AddRef();
    display->AddRef();
    disp->SetContext(this);

    DefaultState();
    contextID = 0;

    extGamma = new gxmExtGamma(display);

    // set buffer sizes for this sample
    const uint32_t patcherBufferSize = 64 * 1024;
    const uint32_t patcherVertexUsseSize = 64 * 1024;
    const uint32_t patcherFragmentUsseSize = 64 * 1024;

    // allocate memory for buffers and USSE code
    radMemorySetAllocationName("patcherBuffer");
    patcherBuffer = radMemorySpaceAlloc(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        patcherBufferSize);

    uint32_t patcherVertexUsseOffset;
    void* patcherVertexUsse = device->vertexUsseAlloc(
        patcherVertexUsseSize,
        &patcherVertexUsseUid,
        &patcherVertexUsseOffset);
    uint32_t patcherFragmentUsseOffset;
    void* patcherFragmentUsse = device->fragmentUsseAlloc(
        patcherFragmentUsseSize,
        &patcherFragmentUsseUid,
        &patcherFragmentUsseOffset);

    // create a shader patcher
    SceGxmShaderPatcherParams patcherParams;
    memset(&patcherParams, 0, sizeof(SceGxmShaderPatcherParams));
    patcherParams.userData = NULL;
    patcherParams.hostAllocCallback = &patcherHostAlloc;
    patcherParams.hostFreeCallback = &patcherHostFree;
    patcherParams.bufferAllocCallback = NULL;
    patcherParams.bufferFreeCallback = NULL;
    patcherParams.bufferMem = patcherBuffer;
    patcherParams.bufferMemSize = patcherBufferSize;
    patcherParams.vertexUsseAllocCallback = NULL;
    patcherParams.vertexUsseFreeCallback = NULL;
    patcherParams.vertexUsseMem = patcherVertexUsse;
    patcherParams.vertexUsseMemSize = patcherVertexUsseSize;
    patcherParams.vertexUsseOffset = patcherVertexUsseOffset;
    patcherParams.fragmentUsseAllocCallback = NULL;
    patcherParams.fragmentUsseFreeCallback = NULL;
    patcherParams.fragmentUsseMem = patcherFragmentUsse;
    patcherParams.fragmentUsseMemSize = patcherFragmentUsseSize;
    patcherParams.fragmentUsseOffset = patcherFragmentUsseOffset;
    CHK_GXM(sceGxmShaderPatcherCreate(&patcherParams, &shaderPatcher));

    // use embedded GXP files
    vertexProgram = new gxmProgram(shaderPatcher, p3d::openFile("app0:/shaders/vertex_cg.gxp", false));
    vertexProgram->AddRef();
    colorProgram = new gxmProgram(shaderPatcher, p3d::openFile("app0:/shaders/color_cg.gxp", false));
    colorProgram->AddRef();
    textureProgram = new gxmProgram(shaderPatcher, p3d::openFile("app0:/shaders/texture_cg.gxp", false));
    textureProgram->AddRef();
    alphaTestProgram = new gxmProgram(shaderPatcher, p3d::openFile("app0:/shaders/alpha_cg.gxp", false));
    alphaTestProgram->AddRef();

    vitaJobsStart();

    memset(vertexPrograms, 0, sizeof(vertexPrograms));
    vertexPrograms[0][VARIANT_FULL] = vertexProgram;
    colorNoSpecProgram = textureNoSpecProgram = alphaTestNoSpecProgram = NULL;
    variantsAvailable = hardwareSkinning = false;
    extSkin = NULL;
    currentDrawBuffer = NULL;
    currentVariant = VARIANT_FULL;
    lastPolygonMode = -1;
    LoadShaderVariants();

    radMemorySetAllocationName("dummyVector");
    dummyVector = (pddiVector4*)radMemorySpaceAlloc(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        sizeof(pddiVector4));
    dummyVector->Set(1.0f, 1.0f, 1.0f);
    CHK_GXM(sceGxmSetVertexStream(context, 1, dummyVector));

    defaultShader = new gxmMat(this);
    defaultShader->AddRef();
    clearShader = new gxmMat(this);
    clearShader->AddRef();
    clearShader->SetInt(PDDI_SP_COLOURWRITE, PDDI_WRITE_NONE);

    streamsTail = 0;
    streamsHead = SceGxmNotification{ sceGxmGetNotificationRegion() };
    memset(streams, 0, sizeof(streams));
    memset(primPool, 0, sizeof(primPool));
    memset(primPoolCount, 0, sizeof(primPoolCount));
    primPoolBytes = 0;
    primPoolHits = primPoolMisses = midSceneFlushes = statFrames = 0;
    *sceGxmGetNotificationRegion() = 0;
}

gxmContext::~gxmContext()
{
    DrainPrimPool();
    defaultShader->Release();
    clearShader->Release();
    delete extSkin;
    for(int s = 0; s < 2; s++)
        for(int v = 0; v < NUM_VERTEX_VARIANTS; v++)
            if(vertexPrograms[s][v])
                vertexPrograms[s][v]->Release();
    ReleaseProgram(colorNoSpecProgram);
    ReleaseProgram(textureNoSpecProgram);
    ReleaseProgram(alphaTestNoSpecProgram);
    colorProgram->Release();
    textureProgram->Release();
    alphaTestProgram->Release();

    delete extGamma;

    device->vertexUsseFree(patcherVertexUsseUid);
    device->fragmentUsseFree(patcherFragmentUsseUid);

    CHK_GXM(sceGxmShaderPatcherDestroy(shaderPatcher));
    radMemorySpaceFree(radMemorySpace_User, radMemoryGetCurrentAllocator(), patcherBuffer);

    CHK_GXM(sceGxmUnmapMemory(dummyVector));
    radMemorySpaceFree(radMemorySpace_User, radMemoryGetCurrentAllocator(), dummyVector);

    display->SetContext(NULL);
    display->Release();
    device->Release();
}

// frame synchronisation
void gxmContext::BeginFrame()
{
    pddiBaseContext::BeginFrame();

    FlushVertexStreams();

    // pooled buffers hold patched vertex programs, drop them when the display is reset
    if(display->HasReset())
        DrainPrimPool();

#ifdef RAD_LOG
    if(++statFrames >= 3600)
    {
        rReleasePrintf("[gxm] last %u frames: primPool hits=%u misses=%u (pooled %u KB), mid-scene flushes=%u, vertex uniform uploads=%u reuses=%u\n",
                       statFrames, primPoolHits, primPoolMisses, primPoolBytes / 1024, midSceneFlushes, uniformUploads, uniformReuses);
        statFrames = primPoolHits = primPoolMisses = midSceneFlushes = uniformUploads = uniformReuses = 0;
        if( LogProgress != NULL )
        {
            LogProgress( "periodic (3600 frames)" );
        }
    }
#endif

    CHK_GXM(sceGxmBeginScene(
        context,
        0,
        display->GetRenderTarget(),
        NULL,
        NULL,
        display->GetFragementSyncObj(),
        display->GetColorSurface(),
        display->GetDepthSurface()
    ));
    inScene = true;
    lastPolygonMode = -1;
    InvalidateUniformShadow();          // the uniform ring starts over with every scene

    if(display->HasReset())
    {
        contextID++;

        SyncState(0xffffffff);
    }

    // a new scene starts with the region clip of the last SetScissor() call
    ApplyScissor();

    // An enabled stencil state is sent again at the first draw of the scene, in case the scene
    // start does not keep it. The disabled state is the hardware default, nothing to resend.
    if(stencilHwMode == STENCIL_HW_ON)
    {
        stencilHwMode = STENCIL_HW_UNKNOWN;
        stencilDirty = true;
    }

    projection.Identity();
}

void gxmContext::EndFrame()
{
    pddiBaseContext::EndFrame();

    CHK_GXM(sceGxmEndScene(context, &streamsHead, NULL));
    inScene = false;
    CHK_GXM(sceGxmPadHeartbeat(display->GetColorSurface(), display->GetFragementSyncObj()));
}

// Block until the GPU is idle. Without this, GUI screens that unload a 3D model (phone booth /
// rewards car preview, vehicle gallery, ...) freed its textures and vertex buffers while the
// previous frame, still queued on the GPU, was reading them: the GPU faulted, the display
// froze, the PS button stopped responding and only a hard reset helped (no crash dump, since
// it is not a CPU exception).
void gxmContext::DrawSync()
{
    if(inScene)
    {
        return;
    }
    // sceGxmFinish() returns void in the SDK used by CI, so it cannot go through CHK_GXM.
    sceGxmFinish(context);
    CHK_GXM(sceGxmDisplayQueueFinish());
}

// buffer clearing
void gxmContext::Clear(unsigned bufferMask)
{
    pddiBaseContext::Clear(bufferMask);

    PushState((pddiStateMask)(PDDI_STATE_RENDER | PDDI_STATE_VIEW));
    SetCamera(0.0f, 1.0f, 1.0f, 1.0f);
    SetProjectionMode(PDDI_PROJECTION_DEVICE);
    EnableZBuffer(bufferMask & PDDI_BUFFER_DEPTH);
    SetZCompare(PDDI_COMPARE_ALWAYS);

    PushIdentityMatrix(PDDI_MATRIX_MODELVIEW);
    pddiVector a, b, c, d;
    a.Set(0.0f, 0.0f, state.viewState->clearDepth);
    b.Set(display->GetWidth(), 0.0f, state.viewState->clearDepth);
    c.Set(0.0f, display->GetHeight(), state.viewState->clearDepth);
    d.Set(display->GetWidth(), display->GetHeight(), state.viewState->clearDepth);
    pddiPrimStream* stream = BeginPrims(
        bufferMask & PDDI_BUFFER_COLOUR ? defaultShader : clearShader,
        PDDI_PRIM_TRISTRIP, PDDI_V_C, 4);
    stream->Vertex(&a, state.viewState->clearColour);
    stream->Vertex(&b, state.viewState->clearColour);
    stream->Vertex(&c, state.viewState->clearColour);
    stream->Vertex(&d, state.viewState->clearColour);
    EndPrims(stream);
    PopMatrix(PDDI_MATRIX_MODELVIEW);

    PopState((pddiStateMask)(PDDI_STATE_RENDER | PDDI_STATE_VIEW));
}

void gxmContext::SetupHardwareProjection(void)
{
    switch(state.viewState->projectionMode)
    {
        case PDDI_PROJECTION_DEVICE :
            projection.Identity();
            projection.SetOrthographic(0, display->GetWidth(),
                      display->GetHeight(), 0,
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            sceGxmSetViewport(context,
                display->GetWidth() / 2.0f, display->GetWidth() / 2.0f,
                display->GetHeight() / 2.0f, -display->GetHeight() / 2.0f,
                (state.viewState->zRange[0] + state.viewState->zRange[1]) / 2.0f,
                (state.viewState->zRange[1] - state.viewState->zRange[0]) / 2.0f);
            break;

        case PDDI_PROJECTION_ORTHOGRAPHIC :
            projection.Identity();
            projection.SetOrthographic(-0.5,  0.5,
                      -((1/state.viewState->camera.aspect)/2),  ((1/state.viewState->camera.aspect)/2),
                      (state.viewState->camera.nearPlane),(state.viewState->camera.farPlane));
            sceGxmSetViewport(context,
                              ((state.viewState->viewWindow.left + state.viewState->viewWindow.right) / 2.0f) * display->GetWidth(),
                              (state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth() / 2.0f,
                              ((state.viewState->viewWindow.top + state.viewState->viewWindow.bottom) / 2.0f) * display->GetHeight(),
                              (state.viewState->viewWindow.top - state.viewState->viewWindow.bottom) * display->GetHeight() / 2.0f,
                              (state.viewState->zRange[0] + state.viewState->zRange[1]) / 2.0f,
                              (state.viewState->zRange[1] - state.viewState->zRange[0]) / 2.0f);
            break;

        case PDDI_PROJECTION_PERSPECTIVE :
            projection.Identity();
            projection.SetPerspective(state.viewState->camera.fov,state.viewState->camera.aspect,state.viewState->camera.nearPlane,state.viewState->camera.farPlane);
            sceGxmSetViewport(context,
                            ((state.viewState->viewWindow.left + state.viewState->viewWindow.right) / 2.0f)* display->GetWidth(),
                            (state.viewState->viewWindow.right - state.viewState->viewWindow.left) * display->GetWidth() / 2.0f,
                            ((state.viewState->viewWindow.top + state.viewState->viewWindow.bottom) / 2.0f) * display->GetHeight(),
                            (state.viewState->viewWindow.top - state.viewState->viewWindow.bottom) * display->GetHeight() / 2.0f,
                            (state.viewState->zRange[0] + state.viewState->zRange[1]) / 2.0f,
                            (state.viewState->zRange[1] - state.viewState->zRange[0]) / 2.0f);
            break;
        default:
            PDDIASSERTMSG(0, "Bad projection mode","");
            break;
    }
}

void gxmContext::LoadHardwareMatrix(pddiMatrixType id)
{
}

// viewport clipping
void gxmContext::SetScissor(pddiRect* rect)
{
    pddiBaseContext::SetScissor(rect);
    ApplyScissor();
}

// The scissor rect is in render target pixels, origin top-left, right/bottom exclusive (same
// convention the GL backend feeds to glScissor). GXM expresses it as a region clip with an
// inclusive max corner. A rect that covers the whole target turns the clip off, so the common
// full-screen case adds no per-tile clipping work.
void gxmContext::ApplyScissor()
{
    if(!inScene)
        return;

    const int width = display->GetWidth();
    const int height = display->GetHeight();
    const pddiRect& r = state.viewState->scissor;

    const int left   = r.left   < 0 ? 0 : r.left;
    const int top    = r.top    < 0 ? 0 : r.top;
    const int right  = r.right  > width  ? width  : r.right;
    const int bottom = r.bottom > height ? height : r.bottom;

    if(left <= 0 && top <= 0 && right >= width && bottom >= height)
    {
        sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_NONE, 0, 0, width - 1, height - 1);
    }
    else if(right <= left || bottom <= top)
    {
        // empty rect: nothing may be drawn
        sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_ALL, 0, 0, 0, 0);
    }
    else
    {
        sceGxmSetRegionClip(context, SCE_GXM_REGION_CLIP_OUTSIDE, left, top, right - 1, bottom - 1);
    }
}

class gxmPrimStream : public pddiPrimStream
{
public:
    pddiPrimBuffer* buffer;
    pddiPrimBufferStream* stream;

    void Coord(float x, float y, float z)  
    {
        stream->Coord(x, y, z);
    }

    void Normal(float x, float y, float z) 
    {
        stream->Normal(x, y, z);
    }

    void Colour(pddiColour colour, int channel = 0)
    {
        stream->Colour(colour, channel);
    }

    void UV(float u, float v, int channel = 0) 
    { 
        stream->UV(u, v, channel);
    }

    void Specular(pddiColour colour) 
    {
        stream->Specular(colour);
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        stream->Vertex(v, c);
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        stream->Vertex(v, n);
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        stream->Vertex(v, uv);
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        stream->Vertex(v, c, uv);
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        stream->Vertex(v, n, uv);
    }

} thePrimStream;

pddiPrimStream* gxmContext::BeginPrims(pddiShader* mat, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass)
{
    GXM_NAMED_TIMER("GXM: BeginPrims");
    if(!mat)
        mat = defaultShader;
    PDDIASSERT(vertexCount);

    FlushStencilState();
    pddiBaseContext::BeginPrims(mat, primType, vertexType, vertexCount);

    // Buffers are created with a rounded-up capacity so a recycled one fits any nearby count.
    const int capacity = PrimPoolCapacity(primType, vertexCount);
    pddiPrimBufferDesc desc(primType, vertexType, capacity);
    gxmPrimBuffer* pooled = AcquirePrimBuffer(primType, vertexType, vertexCount);
    thePrimStream.buffer = pooled ? pooled : device->NewPrimBuffer(&desc);
    if(((gxmPrimBuffer*)thePrimStream.buffer)->Poolable())
        ((gxmPrimBuffer*)thePrimStream.buffer)->SetDrawCount(vertexCount);
    thePrimStream.stream = thePrimStream.buffer->Lock();

    if((streamsHead.value + 1) % BUFFERED_VERTS == streamsTail)
    {
        rDebugString("Vertex stream ring-buffer is full! Waiting for mid-scene flush, consider increasing BUFFERED_VERTS.");
        MidSceneFlushAndWait();
    }
    streamsHead.value = (streamsHead.value + 1) % BUFFERED_VERTS;
    streams[streamsHead.value] = thePrimStream.buffer;

    currentDrawBuffer = (gxmPrimBuffer*)thePrimStream.buffer;
    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    material->SetMaterial();

    return &thePrimStream;
}

void gxmContext::EndPrims(pddiPrimStream* stream)
{
    GXM_DRAW_TIMER();
    pddiBaseContext::EndPrims(stream);
    gxmPrimStream* gxmstream = (gxmPrimStream*)stream;
    gxmstream->buffer->Unlock(thePrimStream.stream);
    ((gxmPrimBuffer*)gxmstream->buffer)->Display();
}

class gxmPrimBufferStream : public pddiPrimBufferStream
{
public:
    gxmPrimBuffer* buffer;

    gxmPrimBufferStream(gxmPrimBuffer* b)
    {
        buffer = b;
    }

    void Next(void)  
    {
        if(buffer->coord)
            buffer->coord = (float*)((char*)buffer->coord + buffer->stride);

        if(buffer->normal)
            buffer->normal = (float*)((char*)buffer->normal + buffer->stride);

        if(buffer->uv)
            buffer->uv = (float*)((char*)buffer->uv + buffer->stride);

        if(buffer->colour)
            buffer->colour += buffer->stride;

        if(buffer->skinWeights)
            buffer->skinWeights = (float*)((char*)buffer->skinWeights + buffer->stride);

        if(buffer->skinIndices)
            buffer->skinIndices += buffer->stride;

        buffer->total++;
        PDDIASSERT(buffer->total <= buffer->allocated);
    }

    void Position(float x, float y, float z)  
    { 
        buffer->coord[0] = x;
        buffer->coord[1] = y;
        buffer->coord[2] = z;
        Next();
    }

    void Normal(float x, float y, float z) 
    { 
        buffer->normal[0] = x;
        buffer->normal[1] = y;
        buffer->normal[2] = z;
    }

    void Colour(pddiColour colour, int channel = 0)         
    {
        // HBW: Multiple CBVs not yet implemented.  For now just ignore channel.
        buffer->colour[0] = colour.Red();
        buffer->colour[1] = colour.Green();
        buffer->colour[2] = colour.Blue();
        buffer->colour[3] = colour.Alpha();
    }

    void TexCoord1(float u, int channel = 0) {}

    void TexCoord2(float u, float v, int channel = 0) 
    { 
        if(channel == 0)
        {
            buffer->uv[0] = u;
            buffer->uv[1] = v;
        }
    }

    void TexCoord3(float u, float v, float s, int channel = 0) {}
    void TexCoord4(float u, float v, float s, float t, int channel = 0) {}

    void Specular(pddiColour colour) 
    {
        //
    }

    void SkinIndices(unsigned a, unsigned b, unsigned c, unsigned d)
    {
        if(buffer->skinIndices)
        {
            buffer->skinIndices[0] = (unsigned char)a;
            buffer->skinIndices[1] = (unsigned char)b;
            buffer->skinIndices[2] = (unsigned char)c;
            buffer->skinIndices[3] = (unsigned char)d;
        }
    }

    void SkinWeights(float a, float b, float c)
    {
        if(buffer->skinWeights)
        {
            buffer->skinWeights[0] = a;
            buffer->skinWeights[1] = b;
            buffer->skinWeights[2] = c;
        }
    }

    bool WriteSkinnedVertices(const float* packed, unsigned count, bool withNormals)
    {
        // position and normal are next to each other in the vertex, see gxmPrimBuffer's constructor
        const unsigned bytes = withNormals ? 24 : 12;
        char* out = (char*)buffer->coord;
        for(unsigned i = 0; i < count; i++)
        {
            memcpy(out, packed, bytes);
            out += buffer->stride;
            packed += bytes / sizeof(float);
        }
        coordsWritten(count);
        return true;
    }

    void coordsWritten(unsigned count)
    {
        // same bookkeeping as 'count' calls of Next()
        const unsigned bytes = count * buffer->stride;
        if(buffer->coord)
            buffer->coord = (float*)((char*)buffer->coord + bytes);
        if(buffer->normal)
            buffer->normal = (float*)((char*)buffer->normal + bytes);
        if(buffer->uv)
            buffer->uv = (float*)((char*)buffer->uv + bytes);
        if(buffer->colour)
            buffer->colour += bytes;
        if(buffer->skinWeights)
            buffer->skinWeights = (float*)((char*)buffer->skinWeights + bytes);
        if(buffer->skinIndices)
            buffer->skinIndices += bytes;
        buffer->total += count;
    }

    void Vertex(pddiVector* v, pddiColour c) 
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector2* uv)
    {
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiColour c, pddiVector2* uv)
    {
        buffer->colour[0] = c.Red();
        buffer->colour[1] = c.Green();
        buffer->colour[2] = c.Blue();
        buffer->colour[3] = c.Alpha();
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    void Vertex(pddiVector* v, pddiVector* n, pddiVector2* uv)
    {
        buffer->normal[0] = n->x;
        buffer->normal[1] = n->y;
        buffer->normal[2] = n->z;
        buffer->uv[0] = uv->u;
        buffer->uv[1] = uv->v;
        buffer->coord[0] = v->x;
        buffer->coord[1] = v->y;
        buffer->coord[2] = v->z;
        Next();
    }

    bool CheckMemImageVersion(int version) { return false; }
    void* GetMemImagePtr()                 { return NULL; }
    unsigned GetMemImageLength()           { return 0; }

};

gxmPrimBuffer::gxmPrimBuffer(gxmContext* c, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex) : context(c)
{
    stream = new gxmPrimBufferStream(this);

    total = allocated = stride = nStrips = 0;
    coord = normal = uv = NULL;
    colour = NULL;
    skinWeights = NULL;
    skinIndices = NULL;
    memset(vertexShaders, 0, sizeof(vertexShaders));
    memset(states, 0, sizeof(states));
    skinned = context->hardwareSkinning && (vertexFormat & (PDDI_V_WEIGHTS | PDDI_V_INDICES)) == (PDDI_V_WEIGHTS | PDDI_V_INDICES);
    strips = NULL;
    indices = NULL;

    valid = false;
    buffer = 0;

    primType = type;
    vertexType = vertexFormat;

    allocated = nVertex;
    
    stride = skinned ? 52 : 36;

    mem = stride * nVertex;
    PDDIASSERT(mem);
    radMemorySetAllocationName("gxmPrimBuffer");
    buffer = (uint8_t*)radMemorySpaceAlloc(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        mem);

    unsigned char* ptr = buffer;
    coord = (float*)ptr;
    ptr += 12;
    
    if(vertexFormat & PDDI_V_NORMAL)
    {
        normal = (float*)ptr;
        ptr += 12;
    }
    
    if(vertexFormat & 0xf)
    {
        uv = (float*)ptr;
        ptr += 8;
    }
    
    if(vertexFormat & PDDI_V_COLOUR)
    {
        colour = ptr;
        ptr += 4;
    }

    if(skinned)
    {
        // Attributes follow the others, like gxmProgram::PatchVertexShader expects. Default: all
        // weight on the first palette entry, for meshes without weight/index chunks.
        skinWeights = (float*)ptr;
        ptr += 12;
        skinIndices = ptr;
        ptr += 4;
        for(int i = 0; i < nVertex; i++)
        {
            float* w = (float*)((char*)skinWeights + i * stride);
            w[0] = 1.0f;
            w[1] = 0.0f;
            w[2] = 0.0f;
            memset(skinIndices + i * stride, 0, 4);
        }
    }

    indexCount = nIndex > 0 ? nIndex : nVertex;
    drawCount = indexCount;
    if(primType == PDDI_PRIM_LINESTRIP)
    {
        PDDIASSERT(!nIndex);
        indexCount *= 2;
    }

    PDDIASSERT(indexCount);
    radMemorySetAllocationName("gxmPrimBuffer");
    indices = (uint16_t*)radMemorySpaceAlloc(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        indexCount * sizeof(uint16_t));

    if(!nIndex)
    {
        if(primType == PDDI_PRIM_LINESTRIP)
        {
            for(int i = 0; i < nVertex; i++)
            {
                indices[i * 2 + 0] = i;
                indices[i * 2 + 1] = i + 1;
            }
        }
        else
        {
            for(int i = 0; i < nVertex; i++)
                indices[i] = i;
        }
    }

    stateSize = 0;
    GetVertexShader(gxmContext::VARIANT_FULL);

    nStrips = 0;
    strips = NULL;

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, 1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, mem / 1024.0f);
}

gxmPrimBuffer::~gxmPrimBuffer()
{
    delete stream;

    radMemorySpaceFree(radMemorySpace_User, radMemoryGetCurrentAllocator(), buffer);
    if(indices)
        radMemorySpaceFree(radMemorySpace_User, radMemoryGetCurrentAllocator(), indices);
    for(int v = 0; v < gxmContext::NUM_VERTEX_VARIANTS; v++)
        if(states[v])
            radMemorySpaceFreeAligned(radMemorySpace_User, radMemoryGetCurrentAllocator(), states[v]);

    context->ADD_STAT(PDDI_STAT_BUFFERED_COUNT, -1);
    context->ADD_STAT(PDDI_STAT_BUFFERED_ALLOC, -mem / 1024.0f);
}

SceGxmVertexProgram* gxmPrimBuffer::GetVertexShader(int variant)
{
    if(vertexShaders[variant] == NULL)
    {
        gxmProgram* program = context->vertexPrograms[skinned ? 1 : 0][variant];
        SceGxmVertexProgram* shader = program->PatchVertexShader(vertexType, stride);
        const unsigned size = sceGxmGetPrecomputedDrawSize(shader);
        radMemorySetAllocationName("gxmPrimBuffer");
        states[variant] = (uint8_t*)radMemorySpaceAllocAligned(
            radMemorySpace_User,
            radMemoryGetCurrentAllocator(),
            size,
            16);
        stateSize += size;
        CHK_GXM(sceGxmPrecomputedDrawInit(&precomputed[variant], shader, states[variant]));
        const void* streams[] = { buffer, context->dummyVector };
        CHK_GXM(sceGxmPrecomputedDrawSetAllVertexStreams(&precomputed[variant], streams));
        sceGxmPrecomputedDrawSetParams(&precomputed[variant], primTypeTable[primType], SCE_GXM_INDEX_FORMAT_U16, indices, drawCount);
        vertexShaders[variant] = shader;
    }
    return vertexShaders[variant];
}

pddiPrimBufferStream* gxmPrimBuffer::Lock()
{
    total = 0;
    return stream;
}

void gxmPrimBuffer::Unlock(pddiPrimBufferStream* stream)
{
    if(coord)
        coord = (float*)((char*)coord - total * stride);

    if(normal)
        normal = (float*)((char*)normal - total * stride);

    if(uv)
        uv = (float*)((char*)uv - total * stride);

    if(colour)
        colour -= total * stride;

    if(skinWeights)
        skinWeights = (float*)((char*)skinWeights - total * stride);

    if(skinIndices)
        skinIndices -= total * stride;

    valid = false;
}

unsigned char* gxmPrimBuffer::LockIndexBuffer()
{
    PDDIASSERT(0);
    return NULL;
}

void gxmPrimBuffer::UnlockIndexBuffer(int count)
{
    PDDIASSERT(0);
}

void gxmPrimBuffer::SetIndices(unsigned short* i, int count)
{
    PDDIASSERT(count <= (int)indexCount);
    memcpy(indices, i, count * sizeof(unsigned short));
    valid = false;
}

void gxmPrimBuffer::SetDrawCount(unsigned nVertex)
{
    PDDIASSERT(nVertex <= indexCount);
    if(nVertex != drawCount)
    {
        drawCount = nVertex;
        for(int v = 0; v < gxmContext::NUM_VERTEX_VARIANTS; v++)
            if(vertexShaders[v])
                sceGxmPrecomputedDrawSetParams(&precomputed[v], primTypeTable[primType], SCE_GXM_INDEX_FORMAT_U16, indices, drawCount);
    }
}

void gxmPrimBuffer::Display(void)
{
    if(!valid)
    {
        valid = true;
    }

    if(context->lastPolygonMode != (int)polyModeTable[primType])
    {
        context->lastPolygonMode = (int)polyModeTable[primType];
        sceGxmSetFrontPolygonMode(context->context, polyModeTable[primType]);
    }
    CHK_GXM(sceGxmDrawPrecomputed(context->context, &precomputed[context->currentVariant]));
}

/*
protected:
    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;

};
*/

void gxmContext::DrawPrimBuffer(pddiShader* mat, pddiPrimBuffer* buffer)
{
    {
        GXM_NAMED_TIMER("GXM: SetMaterial");
        if(!mat)
            mat = defaultShader;

        FlushStencilState();

        pddiBaseShader* material = (pddiBaseShader*)mat;
        ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
        currentDrawBuffer = (gxmPrimBuffer*)buffer;
        material->SetMaterial();
    }

    GXM_DRAW_TIMER();
    ((gxmPrimBuffer*)buffer)->Display();
}

void gxmContext::DrawSkinned(pddiShader* mat, pddiPrimBuffer* buffer, const pddiMatrix* const* palette, unsigned count)
{
    GXM_DRAW_TIMER();
    if(!mat)
        mat = defaultShader;

    FlushStencilState();

    pddiBaseShader* material = (pddiBaseShader*)mat;
    ADD_STAT(PDDI_STAT_MATERIAL_OPS, !material->IsCurrent());
    currentDrawBuffer = (gxmPrimBuffer*)buffer;

    // The palette is part of the uniform buffer, so this draw always needs a fresh one, and the
    // next draw must not reuse it as if it held only what the shadow knows about.
    InvalidateUniformShadow();
    vertexUniformBuffer = NULL;
    material->SetMaterial();
    if(vertexUniformBuffer != NULL)
    {
        vertexProgram->SetBones(vertexUniformBuffer, palette, count);
        ((gxmPrimBuffer*)buffer)->Display();
    }
    InvalidateUniformShadow();
}

int gxmContext::GetVertexVariant(const gxmTextureEnv* texEnv) const
{
    if(!variantsAvailable)
        return VARIANT_FULL;
    if(!texEnv->lit)
        return g_gxmFadeAlpha < 255 ? VARIANT_DIFFUSE : VARIANT_UNLIT;    // the unlit one has no alpha uniform
    const pddiColour s = texEnv->specular;
    return (s.Red() | s.Green() | s.Blue()) != 0 ? VARIANT_FULL : VARIANT_DIFFUSE;
}

void gxmContext::ReleaseProgram(gxmProgram*& program)
{
    if(program)
    {
        program->Release();
        program = NULL;
    }
}

static gxmProgram* MakeProgram(SceGxmShaderPatcher* patcher, gxmShaderId id)
{
    unsigned size = 0;
    void* gxp = gxmShaderLoad(id, &size);
    if(gxp == NULL)
    {
        rReleasePrintf("[gxm] shader variant %d is not available\n", (int)id);
        return NULL;
    }
    gxmProgram* program = new gxmProgram(patcher, gxp, size);
    program->AddRef();
    gxmShaderBuildFree(gxp);
    return program;
}

void gxmContext::LoadShaderVariants()
{
    if(!g_gxmFastShaders)
    {
        rReleasePrintf("[gxm] shader variants off (fastshaders=0)\n");
        return;
    }

    // All of a set or none of it: a vertex variant is useless without its fragment programs.
    rReleasePrintf("[gxm] loading shader variants\n");
    gxmProgram* diffuse = MakeProgram(shaderPatcher, GXM_SHADER_V_DIFFUSE);
    gxmProgram* unlit = MakeProgram(shaderPatcher, GXM_SHADER_V_UNLIT);
    colorNoSpecProgram = MakeProgram(shaderPatcher, GXM_SHADER_F_COLOR_NOSPEC);
    textureNoSpecProgram = MakeProgram(shaderPatcher, GXM_SHADER_F_TEXTURE_NOSPEC);
    alphaTestNoSpecProgram = MakeProgram(shaderPatcher, GXM_SHADER_F_ALPHA_NOSPEC);
    if(diffuse && unlit && colorNoSpecProgram && textureNoSpecProgram && alphaTestNoSpecProgram)
    {
        vertexPrograms[0][VARIANT_DIFFUSE] = diffuse;
        vertexPrograms[0][VARIANT_UNLIT] = unlit;
        variantsAvailable = true;

        if(g_gxmHardwareSkinning)
        {
            gxmProgram* skin[NUM_VERTEX_VARIANTS] = {
                MakeProgram(shaderPatcher, GXM_SHADER_V_SKIN_FULL),
                MakeProgram(shaderPatcher, GXM_SHADER_V_SKIN_DIFFUSE),
                MakeProgram(shaderPatcher, GXM_SHADER_V_SKIN_UNLIT)
            };
            bool ok = true;
            for(int v = 0; v < NUM_VERTEX_VARIANTS; v++)
                ok = ok && skin[v] && skin[v]->HasBones();
            if(ok)
            {
                for(int v = 0; v < NUM_VERTEX_VARIANTS; v++)
                    vertexPrograms[1][v] = skin[v];
                hardwareSkinning = true;
                extSkin = new gxmHardwareSkinning(this);
            }
            else
            {
                for(int v = 0; v < NUM_VERTEX_VARIANTS; v++)
                    ReleaseProgram(skin[v]);
            }
        }
    }
    else
    {
        ReleaseProgram(diffuse);
        ReleaseProgram(unlit);
        ReleaseProgram(colorNoSpecProgram);
        ReleaseProgram(textureNoSpecProgram);
        ReleaseProgram(alphaTestNoSpecProgram);
    }
    gxmShaderSetInUse(variantsAvailable, hardwareSkinning);
    rReleasePrintf("[gxm] shader variants: %s, hardware skinning: %s\n",
                   variantsAvailable ? "on" : "off (not compiled yet, see the Options menu)",
                   hardwareSkinning ? "on" : "off (NEON software skinning)");
}

// lighting

int gxmContext::GetMaxLights(void)
{
    return PDDI_MAX_LIGHTS;
}

void gxmContext::SetupHardwareLight(int handle)
{
    if(handle < 0 || handle >= PDDI_MAX_LIGHTS)
        return;

    pddiMatrixStack* stack = state.matrixStack[PDDI_MATRIX_MODELVIEW];
    if(!stack)
        return;

    // world space -> view space with the matrix current right now (the view matrix)
    const pddiLight* l = &state.lightingState->light[handle];
    const pddiMatrix* view = stack->Top();
    lightViewDirection[handle].Rotate(l->worldDirection, *view);
    lightViewPosition[handle].Transform(l->worldPosition, *view);
}

void gxmContext::SetAmbientLight(pddiColour col)
{
    pddiBaseContext::SetAmbientLight(col);
}

// backface culling
SceGxmCullMode cullModeTable[3] =
{
    SCE_GXM_CULL_NONE, // PDDI_CULL_NONE
    SCE_GXM_CULL_CW,   // PDDI_CULL_NORMAL
    SCE_GXM_CULL_CCW   // PDDI_CULL_INVERTED
};

// enable == true: apply the cull mode of the render state (single sided material),
// enable == false: draw both faces (two sided material). The caller passes !twoSided.
// This used to be inverted, so single sided materials were never culled and the culling the
// shadow volume passes select with SetCullMode() was overridden by every material change.
void gxmContext::EnableCulling(bool enable)
{
    sceGxmSetCullMode(context, enable ? cullModeTable[state.renderState->cullMode] : SCE_GXM_CULL_NONE);
}

void gxmContext::SetCullMode(pddiCullMode mode)
{
    pddiBaseContext::SetCullMode(mode);
    sceGxmSetCullMode(context, cullModeTable[mode]);
}

// z-buffer control
SceGxmDepthFunc compTable[8] = {
    SCE_GXM_DEPTH_FUNC_NEVER,
    SCE_GXM_DEPTH_FUNC_ALWAYS,
    SCE_GXM_DEPTH_FUNC_LESS,
    SCE_GXM_DEPTH_FUNC_LESS_EQUAL,
    SCE_GXM_DEPTH_FUNC_GREATER,
    SCE_GXM_DEPTH_FUNC_GREATER_EQUAL,
    SCE_GXM_DEPTH_FUNC_EQUAL,
    SCE_GXM_DEPTH_FUNC_NOT_EQUAL,
};

void gxmContext::SetColourWrite(bool red, bool green, bool blue, bool alpha)
{
    pddiBaseContext::SetColourWrite(red, green, blue, alpha);
    // GXM has no global colour mask. The base class keeps the state and gxmMat folds it into
    // the blend state of the fragment program it selects (gxmMat::SelectFragmentShader).
}

int gxmContext::GetColourWriteMask() const
{
    const pddiRenderState* rs = state.renderState;
    return (rs->redWrite   ? PDDI_WRITE_RED   : 0) |
           (rs->greenWrite ? PDDI_WRITE_GREEN : 0) |
           (rs->blueWrite  ? PDDI_WRITE_BLUE  : 0) |
           (rs->alphaWrite ? PDDI_WRITE_ALPHA : 0);
}

void gxmContext::EnableZBuffer(bool enable)
{
    pddiBaseContext::EnableZBuffer(enable);
    if(enable)
    {
        sceGxmSetFrontDepthFunc(context, compTable[state.renderState->zCompare]);
        sceGxmSetFrontDepthWriteEnable(context, state.renderState->zWrite ?
            SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
    }
    else
    {
        sceGxmSetFrontDepthFunc(context, SCE_GXM_DEPTH_FUNC_ALWAYS);
        sceGxmSetFrontDepthWriteEnable(context, SCE_GXM_DEPTH_WRITE_DISABLED);
    }
}


void gxmContext::SetZCompare(pddiCompareMode compareMode)
{
    pddiBaseContext::SetZCompare(compareMode);
    sceGxmSetFrontDepthFunc(context, compTable[compareMode]);
}

void gxmContext::SetZWrite(bool b)
{
    pddiBaseContext::SetZWrite(b);
    sceGxmSetFrontDepthWriteEnable(context, b ? SCE_GXM_DEPTH_WRITE_ENABLED : SCE_GXM_DEPTH_WRITE_DISABLED);
}

void gxmContext::SetZBias(float bias)
{
    pddiBaseContext::SetZBias(bias);
//TODO : Figure out how ro do this
}

void gxmContext::SetZRange(float n, float f)
{
    pddiBaseContext::SetZRange(n,f);
    SetupHardwareProjection();
}

// stencil buffer control
SceGxmStencilFunc stencilTable[8] = {
    SCE_GXM_STENCIL_FUNC_NEVER,
    SCE_GXM_STENCIL_FUNC_ALWAYS,
    SCE_GXM_STENCIL_FUNC_LESS,
    SCE_GXM_STENCIL_FUNC_LESS_EQUAL,
    SCE_GXM_STENCIL_FUNC_GREATER,
    SCE_GXM_STENCIL_FUNC_GREATER_EQUAL,
    SCE_GXM_STENCIL_FUNC_EQUAL,
    SCE_GXM_STENCIL_FUNC_NOT_EQUAL,
};
SceGxmStencilOp stencilOpTable[6] = {
    SCE_GXM_STENCIL_OP_KEEP,
    SCE_GXM_STENCIL_OP_ZERO,
    SCE_GXM_STENCIL_OP_REPLACE,
    SCE_GXM_STENCIL_OP_INCR,
    SCE_GXM_STENCIL_OP_DECR,
    SCE_GXM_STENCIL_OP_INVERT
};

void gxmContext::EnableStencilBuffer(bool enable)
{
    pddiBaseContext::EnableStencilBuffer(enable);
    SetupHardwareStencil();
}

// GXM has no stencil enable switch and tests front and back faces with separate state, so both
// sides are always programmed together: the PDDI state (like glStencilFunc/glStencilOp) applies to
// every face, and the shadow volume passes (INCR on one facing, DECR on the other, selected only
// by the cull mode) need it on both. "Disabled" is ALWAYS/KEEP with empty masks on both sides.
void gxmContext::ApplyStencil(void)
{
    stencilDirty = false;
    const pddiStencilState* s = state.stencilState;

    if(s->enabled)
    {
        const SceGxmStencilFunc func = stencilTable[s->compare];
        const SceGxmStencilOp fail = stencilOpTable[s->failOp];
        const SceGxmStencilOp zFail = stencilOpTable[s->zFailOp];
        const SceGxmStencilOp zPass = stencilOpTable[s->zPassOp];
        const uint8_t compareMask = (uint8_t)s->mask;
        const uint8_t writeMask = (uint8_t)s->writeMask;

        sceGxmSetFrontStencilFunc(context, func, fail, zFail, zPass, compareMask, writeMask);
        sceGxmSetBackStencilFunc(context, func, fail, zFail, zPass, compareMask, writeMask);
        sceGxmSetFrontStencilRef(context, (uint8_t)s->ref);
        sceGxmSetBackStencilRef(context, (uint8_t)s->ref);
        stencilHwMode = STENCIL_HW_ON;
    }
    else if(stencilHwMode != STENCIL_HW_OFF)
    {
        // only touch the hardware if it was not already programmed as disabled: the sync of
        // a pushed/popped stencil state would otherwise resend it on every pass
        sceGxmSetFrontStencilFunc(context,
            SCE_GXM_STENCIL_FUNC_ALWAYS,
            SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
            0, 0);
        sceGxmSetBackStencilFunc(context,
            SCE_GXM_STENCIL_FUNC_ALWAYS,
            SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP, SCE_GXM_STENCIL_OP_KEEP,
            0, 0);
        stencilHwMode = STENCIL_HW_OFF;
    }
}
        
void gxmContext::SetStencilCompare(pddiCompareMode compare)
{
    pddiBaseContext::SetStencilCompare(compare);
    SetupHardwareStencil();
}

void gxmContext::SetStencilRef(int ref)
{
    pddiBaseContext::SetStencilRef(ref);
    SetupHardwareStencil();
}

void gxmContext::SetStencilMask(unsigned mask)
{
    pddiBaseContext::SetStencilMask(mask);
    SetupHardwareStencil();
}

void gxmContext::SetStencilWriteMask(unsigned mask)
{
    pddiBaseContext::SetStencilWriteMask(mask);
    SetupHardwareStencil();
}

void gxmContext::SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp)
{
    pddiBaseContext::SetStencilOp(failOp, zFailOp, zPassOp);
    SetupHardwareStencil();
}

void gxmContext::SetFillMode(pddiFillMode mode)
{
    pddiBaseContext::SetFillMode(mode);
}

// fog
void gxmContext::EnableFog(bool enable)
{
    pddiBaseContext::EnableFog(enable);
}

void gxmContext::SetFog(pddiColour colour, float start, float end)
{
    pddiBaseContext::SetFog(colour,start,end);

    float fog[4];
    fog[0] = float(colour.Red()) / 255;
    fog[1] = float(colour.Green()) / 255;
    fog[2] = float(colour.Blue()) / 255;
    fog[3] = float(colour.Alpha()) / 255;
}

int gxmContext::GetMaxTextureDimension(void)
{
    return 4096;
}

pddiExtension* gxmContext::GetExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GAMMACONTROL :
            return extGamma;
        case PDDI_EXT_HARDWARE_SKINNING :
            if(hardwareSkinning)
                return extSkin;
            break;
    }

    return pddiBaseContext::GetExtension(extID);
}

bool gxmContext::VerifyExtension(unsigned extID)
{ 
    switch(extID)
    {
        case PDDI_EXT_GAMMACONTROL :
            return true;
        case PDDI_EXT_HARDWARE_SKINNING :
            return hardwareSkinning;
    }

    return pddiBaseContext::VerifyExtension(extID);
}

void  gxmContext::BeginTiming(void)
{
    display->BeginTiming();
}

float gxmContext::EndTiming(void)
{
    return display->EndTiming();
}

void gxmContext::SetVertexShader(SceGxmVertexProgram* vert)
{
    if(vert != vertexShader)
    {
        vertexShader = vert;
        InvalidateUniformShadow();      // a different vertex program: do not assume its uniforms carry over
        sceGxmSetVertexProgram(context, vert);
    }
}

void gxmContext::FlushVertexStreams()
{
    while(streamsTail != *streamsHead.address)
    {
        if(streams[streamsTail])
            RecyclePrimBuffer(streams[streamsTail]);
        streams[streamsTail] = NULL;
        streamsTail = (streamsTail + 1) % BUFFERED_VERTS;
    }
}

int gxmContext::PrimPoolCapacity(pddiPrimType type, int nVertex)
{
    // line strips use a paired index buffer and are never pooled, so they keep exact size
    if(nVertex <= 0 || nVertex > PRIM_POOL_MAX_VERTS || type == PDDI_PRIM_LINESTRIP)
        return nVertex;

    int capacity = PRIM_POOL_MIN_VERTS;
    while(capacity < nVertex)
        capacity <<= 1;
    return capacity;
}

gxmPrimBuffer* gxmContext::AcquirePrimBuffer(pddiPrimType type, unsigned vertexType, int nVertex)
{
    if(nVertex <= 0 || nVertex > PRIM_POOL_MAX_VERTS || type == PDDI_PRIM_LINESTRIP)
        return NULL;

    const int capacity = PrimPoolCapacity(type, nVertex);
    const unsigned b = gxmPrimBuffer::PoolBucket(type, vertexType, (unsigned)capacity);
    for(int i = (int)primPoolCount[b] - 1; i >= 0; --i)
    {
        gxmPrimBuffer* buf = primPool[b][i];
        if(buf->Matches(type, vertexType, capacity))
        {
            primPool[b][i] = primPool[b][--primPoolCount[b]];
            primPoolBytes -= buf->PoolBytes();
            ++primPoolHits;
            return buf;
        }
    }
    ++primPoolMisses;
    return NULL;
}

// Called once the GPU has finished with an immediate-mode buffer (same point where it used
// to be deleted). Keeps small ones for reuse; everything else is released as before.
void gxmContext::RecyclePrimBuffer(pddiPrimBuffer* buffer)
{
    gxmPrimBuffer* buf = (gxmPrimBuffer*)buffer;
    if(buf->Poolable() && buf->IsUnshared())
    {
        const unsigned b = buf->PoolBucket();
        if(primPoolCount[b] < PRIM_POOL_DEPTH && primPoolBytes + buf->PoolBytes() <= PRIM_POOL_MAX_BYTES)
        {
            primPool[b][primPoolCount[b]++] = buf;
            primPoolBytes += buf->PoolBytes();
            return;
        }
    }
    buffer->Release();
}

void gxmContext::DrainPrimPool()
{
    for(unsigned b = 0; b < PRIM_POOL_BUCKETS; ++b)
    {
        while(primPoolCount[b] > 0)
        {
            gxmPrimBuffer* buf = primPool[b][--primPoolCount[b]];
            primPool[b][primPoolCount[b]] = NULL;
            buf->Release();
        }
    }
    primPoolBytes = 0;
}

gxmProgram* gxmContext::GetFragmentProgram(const gxmTextureEnv* texEnv)
{
    const bool noSpecular = GetVertexVariant(texEnv) != VARIANT_FULL;
    if(texEnv->texture)
    {
        if(texEnv->alphaTest)
            return noSpecular ? alphaTestNoSpecProgram : alphaTestProgram;
        return noSpecular ? textureNoSpecProgram : textureProgram;
    }
    return noSpecular ? colorNoSpecProgram : colorProgram;
}

void gxmContext::MidSceneFlushAndWait()
{
    ++midSceneFlushes;
    InvalidateUniformShadow();
    CHK_GXM(sceGxmMidSceneFlush(context, 0, NULL, &streamsHead));
    CHK_GXM(sceGxmNotificationWait(&streamsHead));
    FlushVertexStreams();
}

bool gxmContext::ReserveVertexUniforms()
{
    int err = sceGxmReserveVertexDefaultUniformBuffer(context, &vertexUniformBuffer);
    if(err == (int)SCE_GXM_ERROR_RESERVE_FAILED)
    {
        MidSceneFlushAndWait();
        err = sceGxmReserveVertexDefaultUniformBuffer(context, &vertexUniformBuffer);
    }
    return CHK_GXM(err);
}

bool gxmContext::ReserveFragmentUniforms()
{
    int err = sceGxmReserveFragmentDefaultUniformBuffer(context, &fragmentUniformBuffer);
    if(err == (int)SCE_GXM_ERROR_RESERVE_FAILED)
    {
        MidSceneFlushAndWait();
        err = sceGxmReserveFragmentDefaultUniformBuffer(context, &fragmentUniformBuffer);
    }
    return CHK_GXM(err);
}

void gxmContext::SetTextureEnvironment(const gxmTextureEnv* texEnv, gxmProgram* fragProgram)
{
    // Bind the vertex program of this draw: the uniform buffer reserved below is sized for it.
    if(currentDrawBuffer != NULL)
    {
        currentVariant = GetVertexVariant(texEnv);
        vertexProgram = vertexPrograms[currentDrawBuffer->IsSkinned() ? 1 : 0][currentVariant];
        SetVertexShader(currentDrawBuffer->GetVertexShader(currentVariant));
    }

#if RAD_GXM_UNIFORM_REUSE
    // Work out what this draw needs, and only reserve + fill a new vertex uniform buffer if it
    // differs from what the bound one holds. The values are compared after the same
    // transformations the upload applies (unlit draws turn every light off, etc).
    // Most draws have a new modelview: find that out first, before anything gets computed or
    // copied for the comparison (the rest of 'now' is needed for the upload either way).
    const pddiMatrix* modelview = state.matrixStack[PDDI_MATRIX_MODELVIEW]->Top();
    bool same = uniformShadow.valid &&
                (!texEnv->lit || uniformShadow.normalWritten) &&    // an unlit buffer holds no normal matrix
                memcmp(&uniformShadow.modelview, modelview, sizeof(pddiMatrix)) == 0 &&
                memcmp(&uniformShadow.projection, &projection, sizeof(pddiMatrix)) == 0 &&
                memcmp(&uniformShadow.ambientLight, &state.lightingState->ambient, sizeof(pddiColour)) == 0;

    VertexUniformShadow now;
    if(texEnv->lit)
    {
        now.material[0] = texEnv->ambient;
        now.material[1] = texEnv->emissive;
        now.material[2] = texEnv->diffuse;
        now.material[3] = texEnv->specular;
    }
    else
    {
        now.material[0] = now.material[1] = now.material[2] = now.material[3] = pddiColour(-1);
    }
    for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
    {
        gxmProgram::ComputeLightUniforms(&state.lightingState->light[i], &lightViewPosition[i], &lightViewDirection[i], texEnv->lit, texEnv->shininess, now.lightDir[i], now.lightAtten[i]);
        now.lightColour[i] = state.lightingState->light[i].colour;
    }

    if(g_gxmFadeAlpha < 255)
        now.material[2].SetAlpha(now.material[2].Alpha() * g_gxmFadeAlpha / 255);

    same = same && memcmp(uniformShadow.material, now.material, sizeof(now.material)) == 0;
    for(int i = 0; same && i < PDDI_MAX_LIGHTS; i++)
    {
        same = memcmp(uniformShadow.lightDir[i], now.lightDir[i], sizeof(float) * 4) == 0;
        if(same && now.lightDir[i][3] >= 0.0f)
        {
            // only enabled lights have their colour / attenuation uploaded (and read)
            same = memcmp(&uniformShadow.lightColour[i], &now.lightColour[i], sizeof(pddiColour)) == 0 &&
                   memcmp(uniformShadow.lightAtten[i], now.lightAtten[i], sizeof(float) * 4) == 0;
        }
    }

    if(same)
    {
        ++uniformReuses;
    }
    else
    {
        if(!ReserveVertexUniforms())
        {
            InvalidateUniformShadow();
            return; // ring still exhausted: skip the uniform upload instead of writing through NULL
        }
        ++uniformUploads;
        for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
            vertexProgram->SetLightValues(vertexUniformBuffer, i, now.lightDir[i], now.lightColour[i], now.lightAtten[i]);
        vertexProgram->SetAmbientLight(vertexUniformBuffer, state.lightingState->ambient);
        vertexProgram->SetModelViewMatrix(vertexUniformBuffer, modelview, texEnv->lit);
        vertexProgram->SetProjectionMatrix(vertexUniformBuffer, &projection);
        vertexProgram->SetMaterialColours(vertexUniformBuffer, now.material[0], now.material[1], now.material[2], now.material[3]);
        now.valid = true;
        now.lit = texEnv->lit;
        now.normalWritten = texEnv->lit;
        now.modelview = *modelview;
        now.projection = projection;
        now.ambientLight = state.lightingState->ambient;
        uniformShadow = now;
    }
#else
    if(!ReserveVertexUniforms())
        return; // ring still exhausted: skip the uniform upload instead of writing through NULL
    for(int i = 0; i < PDDI_MAX_LIGHTS; i++)
        vertexProgram->SetLightState(vertexUniformBuffer, i, &state.lightingState->light[i], &lightViewPosition[i], &lightViewDirection[i], texEnv->lit, texEnv->shininess);
    vertexProgram->SetAmbientLight(vertexUniformBuffer, state.lightingState->ambient);
    vertexProgram->SetModelViewMatrix(vertexUniformBuffer, state.matrixStack[PDDI_MATRIX_MODELVIEW]->Top(), texEnv->lit);
    vertexProgram->SetProjectionMatrix(vertexUniformBuffer, &projection);
    vertexProgram->SetTextureEnvironment(vertexUniformBuffer, texEnv);
#endif
    if (texEnv->alphaTest)
    {
        if(ReserveFragmentUniforms())
            fragProgram->SetAlphaTest(fragmentUniformBuffer, texEnv);
    }
}