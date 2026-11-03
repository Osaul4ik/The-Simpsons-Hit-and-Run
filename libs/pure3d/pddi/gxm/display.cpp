//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================

#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/context.hpp>
#include <pddi/gxm/device.hpp>
#include <pddi/gxm/display.hpp>
#include <pddi/base/debug.hpp>
#include <SDL.h>
#include <radmemory.hpp>
#ifdef RAD_LOG
#include <raddebug.hpp>
#endif

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <vitasdk.h>

// 1 = 60 fps, 2 = 30 fps; defined in code/main/win32main.cpp (set from vita.cfg)
extern int g_gxmFrameRate;

// Enable the following define to allow Razor HUD.
//#define ENABLE_RAZOR_HUD

// Enable the following define to create a Razor capture file.
//#define ENABLE_RAZOR_GPU_CAPTURE

// GXM ring buffer sizes. The defaults (VDM 128 KB, vertex 2 MB, fragment 512 KB) are sized for
// small scenes; this game issues thousands of draws per frame, each reserving a default
// uniform buffer from the vertex ring, and exhausting it fails the draw with
// SCE_GXM_ERROR_RESERVE_FAILED (0x805B0006). The memory comes from radMemorySpace_User.
#ifndef RAD_VITA_GXM_VDM_RING_KB
#define RAD_VITA_GXM_VDM_RING_KB 512
#endif
#ifndef RAD_VITA_GXM_VERTEX_RING_KB
#define RAD_VITA_GXM_VERTEX_RING_KB 4096
#endif
#ifndef RAD_VITA_GXM_FRAGMENT_RING_KB
#define RAD_VITA_GXM_FRAGMENT_RING_KB 1024
#endif
#define GXM_VDM_RING_SIZE      (RAD_VITA_GXM_VDM_RING_KB * 1024)
#define GXM_VERTEX_RING_SIZE   (RAD_VITA_GXM_VERTEX_RING_KB * 1024)
#define GXM_FRAGMENT_RING_SIZE (RAD_VITA_GXM_FRAGMENT_RING_KB * 1024)

// Helper macro to align a value
#define ALIGN(x, a)					(((x) + ((a) - 1)) & ~((a) - 1))

#if defined(ENABLE_RAZOR_GPU_CAPTURE) || defined(ENABLE_RAZOR_HUD)
#include <psp2/sysmodule.h>
#endif
#ifdef ENABLE_RAZOR_GPU_CAPTURE
#include <psp2/razor_capture.h>
#endif

// ---------------------------------------------------------------------------
// GXM driver initialisation
//
// sceGxmInitialize() allocates its own driver memory (parameter buffer etc.) from the
// system pools. radMemorySpaceInitialize() grabs nearly all user RAM and CDRAM for the
// game heaps, so if GXM is initialised afterwards it fails with
// SCE_GXM_ERROR_OUT_OF_MEMORY (0x805B0002). radMemorySpaceInitialize() therefore calls
// radGxmEarlyInit() first (same order vitaGL uses); InitDisplay() calls it again as a
// no-op safety net.
// ---------------------------------------------------------------------------
static bool s_gxmInitialized = false;

bool gxmDisplay::InitializeDriver(void)
{
    if(s_gxmInitialized)
        return true;

    // Default parameter buffer first, then smaller ones, so that a tight memory layout
    // degrades gracefully instead of failing at boot.
    static const uint32_t sizes[] = {
        SCE_GXM_DEFAULT_PARAMETER_BUFFER_SIZE,
        8u * 1024 * 1024,
        4u * 1024 * 1024
    };
    for(uint32_t i = 0; i < sizeof(sizes) / sizeof(sizes[0]); ++i)
    {
        SceGxmInitializeParams params;
        memset(&params, 0, sizeof(params));
        params.flags = 0;
        params.displayQueueMaxPendingCount = kNumColourBuffers - 1;
        params.displayQueueCallback = displayCallback;
        params.displayQueueCallbackDataSize = sizeof(DisplayData);
        params.parameterBufferSize = sizes[i];
        int err = sceGxmInitialize(&params);
#ifdef RAD_LOG
        rReleasePrintf("[gxm] sceGxmInitialize paramBuffer=%u KB -> 0x%08x\n", sizes[i] / 1024, (unsigned)err);
#endif
        if(err == SCE_OK)
        {
            s_gxmInitialized = true;
            return true;
        }
        if(err != (int)SCE_GXM_ERROR_OUT_OF_MEMORY)
            break;
    }
    return false;
}

// C entry point so radcore can call it without depending on pure3d headers.
extern "C" bool radGxmEarlyInit(void)
{
    return gxmDisplay::InitializeDriver();
}

gxmDisplay::gxmDisplay(pddiDisplayInfo* info)
{
    displayInfo = info;
    mode = PDDI_DISPLAY_WINDOW;
    winWidth = 960;
    winHeight = 544;
    winBitDepth = 32;

    context = NULL;

    win = NULL;

    extBGRA = false;

    gammaR = gammaG = gammaB = 1.0f;

    backBufferIndex = 0;
    frontBufferIndex = 0;

    reset = true;
	m_ForceVSync = false;
}

gxmDisplay::~gxmDisplay()
{
    // map external memory heaps
    IRadMemoryHeap* userHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_User, radMemoryGetCurrentAllocator());
    CHK_GXM(sceGxmUnmapMemory(userHeap->GetStartOfMemory()));
    IRadMemoryHeap* cdramHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_Cdram, radMemoryGetCurrentAllocator());
    CHK_GXM(sceGxmUnmapMemory(cdramHeap->GetStartOfMemory()));
    IRadMemoryHeap* phycontHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_Phycont, radMemoryGetCurrentAllocator());
    if(phycontHeap)
        CHK_GXM(sceGxmUnmapMemory(phycontHeap->GetStartOfMemory()));

    /* release and free the device context and rendering context */
    sceGxmDestroyContext(gxm);
    gxmDevice::fragmentUsseFree(fragmentUsseRingBufferUid);
    radMemorySpaceFreeAligned(radMemorySpace_User, radMemoryGetCurrentAllocator(), fragmentRingBuffer);
    radMemorySpaceFreeAligned(radMemorySpace_User, radMemoryGetCurrentAllocator(), vertexRingBuffer);
    radMemorySpaceFreeAligned(radMemorySpace_User, radMemoryGetCurrentAllocator(), vdmRingBuffer);
    for(uint32_t i = 0; i < GetNumColourBuffer(); ++i)
        radMemorySpaceFreeAligned(radMemorySpace_Cdram, radMemoryGetCurrentAllocator(), displayBufferData[i]);
    radMemorySpaceFreeAligned(radMemorySpace_Cdram, radMemoryGetCurrentAllocator(), depthBufferData);
    free(contextParams.hostMem);
    sceGxmTerminate();
    s_gxmInitialized = false;

#if SDL_MAJOR_VERSION < 3
    SDL_SetWindowGammaRamp(win, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2]);
#endif
}

#define KEYPRESSED(x) (GetKeyState((x)) & (1<<(sizeof(int)*8)-1))

long gxmDisplay::ProcessWindowMessage(SDL_Window* win, const SDL_WindowEvent* event)
{
#if SDL_MAJOR_VERSION < 3
    switch(event->event)
    {
        case SDL_WINDOWEVENT_SIZE_CHANGED:
            SDL_GL_GetDrawableSize( win, &winWidth, &winHeight );
            break;

        case SDL_WINDOWEVENT_CLOSE:
            /* release and free the device context and rendering context */
            break;

		default:
            return 0;
    }
#else
    switch(event->type)
    {
        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            SDL_GetWindowSizeInPixels( win, &winWidth, &winHeight );
            break;

        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            /* release and free the device context and rendering context */
            break;

        default:
            return 0;
    }
#endif

    /* return 1 if handled message, 0 if not */
    return 1;
}


void gxmDisplay::SetWindow(SDL_Window* wnd)
{
#if SDL_MAJOR_VERSION < 3
    SDL_GetWindowGammaRamp( wnd, initialGammaRamp[0], initialGammaRamp[1], initialGammaRamp[2] );
#endif
    win = wnd;
}

bool gxmDisplay::InitDisplay(int x, int y, int bpp)
{
    // check we are not trying to init to the same resolution
    if((x == winWidth) &&  (y == winHeight) && (bpp == winBitDepth))
    {
        return true;
    }

    // fill in the relevent portions of the casced display init structure
    displayInit.xsize = x;
    displayInit.ysize = y;
    displayInit.bpp = bpp;

    // do the full init
    return InitDisplay( &displayInit );
}

bool gxmDisplay::InitDisplay(const pddiDisplayInit* init)
{
    displayInit = *init;

    winWidth = init->xsize;
    winHeight = init->ysize;
    winBitDepth = init->bpp;
    msaaMode = init->nSamples >= 4 ? SCE_GXM_MULTISAMPLE_4X :
        init->nSamples >= 2 ? SCE_GXM_MULTISAMPLE_2X : SCE_GXM_MULTISAMPLE_NONE;
    extBGRA = true;

#ifdef ENABLE_RAZOR_HUD
    // Initialize the Razor HUD system.
    // This should be done before the call to sceGxmInitialize().
    sceSysmoduleLoadModule(SCE_SYSMODULE_RAZOR_HUD);
#endif

#ifdef ENABLE_RAZOR_GPU_CAPTURE
    // Initialize the Razor capture system.
    // This should be done before the call to sceGxmInitialize().
    //sceSysmoduleLoadModule(SCE_SYSMODULE_RAZOR_CAPTURE);
    sceKernelLoadStartModule("ux0:data/librazorcapture_es4.suprx", 0, NULL, 0, NULL, NULL);

    // Trigger a capture after 100 frames.
    sceRazorGpuCaptureSetTrigger(10000, "ux0:data/srr2.sgx");
#endif

#ifdef RAD_LOG
    rReleasePrintf("[gxm] InitDisplay %dx%d msaa=%d\n", (int)winWidth, (int)winHeight, (int)msaaMode);
#endif

    // The driver is normally already up (radMemorySpaceInitialize calls radGxmEarlyInit
    // before reserving the game heaps); this is a no-op in that case.
    if(!InitializeDriver())
    {
#ifdef RAD_LOG
        rReleasePrintf("[gxm] sceGxmInitialize failed, cannot create display\n");
#endif
        return false;
    }

    // map external memory heaps
    IRadMemoryHeap* userHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_User, radMemoryGetCurrentAllocator());
    CHK_GXM(sceGxmMapMemory(userHeap->GetStartOfMemory(), userHeap->GetSize(), SCE_GXM_MEMORY_ATTRIB_READ));
    IRadMemoryHeap* cdramHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_Cdram, radMemoryGetCurrentAllocator());
    CHK_GXM(sceGxmMapMemory(cdramHeap->GetStartOfMemory(), cdramHeap->GetSize(), SCE_GXM_MEMORY_ATTRIB_RW));
    IRadMemoryHeap* phycontHeap = (IRadMemoryHeap*)radMemorySpaceGetAllocator(radMemorySpace_Phycont, radMemoryGetCurrentAllocator());
    if(phycontHeap)
        CHK_GXM(sceGxmMapMemory(phycontHeap->GetStartOfMemory(), phycontHeap->GetSize(), SCE_GXM_MEMORY_ATTRIB_RW));

    // allocate ring buffer memory (4 KB aligned, as GXM requires)
    radMemorySetAllocationName("displayBufferData");
    vdmRingBuffer = radMemorySpaceAllocAligned(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        GXM_VDM_RING_SIZE,
        4096);
    radMemorySetAllocationName("vertexRingBuffer");
    vertexRingBuffer = radMemorySpaceAllocAligned(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        GXM_VERTEX_RING_SIZE,
        4096);
    radMemorySetAllocationName("fragmentRingBuffer");
    fragmentRingBuffer = radMemorySpaceAllocAligned(
        radMemorySpace_User,
        radMemoryGetCurrentAllocator(),
        GXM_FRAGMENT_RING_SIZE,
        4096);
    uint32_t fragmentUsseRingBufferOffset;
    void* fragmentUsseRingBuffer = gxmDevice::fragmentUsseAlloc(
        SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE,
        &fragmentUsseRingBufferUid,
        &fragmentUsseRingBufferOffset);

    // set up parameters and create the context
    memset(&contextParams, 0, sizeof(SceGxmContextParams));
    contextParams.hostMem = malloc(SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE);
    contextParams.hostMemSize = SCE_GXM_MINIMUM_CONTEXT_HOST_MEM_SIZE;
    contextParams.vdmRingBufferMem = vdmRingBuffer;
    contextParams.vdmRingBufferMemSize = GXM_VDM_RING_SIZE;
    contextParams.vertexRingBufferMem = vertexRingBuffer;
    contextParams.vertexRingBufferMemSize = GXM_VERTEX_RING_SIZE;
    contextParams.fragmentRingBufferMem = fragmentRingBuffer;
    contextParams.fragmentRingBufferMemSize = GXM_FRAGMENT_RING_SIZE;
    contextParams.fragmentUsseRingBufferMem = fragmentUsseRingBuffer;
    contextParams.fragmentUsseRingBufferMemSize = SCE_GXM_DEFAULT_FRAGMENT_USSE_RING_BUFFER_SIZE;
    contextParams.fragmentUsseRingBufferOffset = fragmentUsseRingBufferOffset;
    if(!CHK_GXM(sceGxmCreateContext(&contextParams, &gxm)))
        return false;

    // set up parameters and create the default render target
    SceGxmRenderTargetParams renderTargetParams;
    memset(&renderTargetParams, 0, sizeof(SceGxmRenderTargetParams));
    renderTargetParams.flags = 0;
    renderTargetParams.width = winWidth;
    renderTargetParams.height = winHeight;
    renderTargetParams.scenesPerFrame = 1;
    renderTargetParams.multisampleMode = msaaMode;
    renderTargetParams.multisampleLocations = 0;
    renderTargetParams.driverMemBlock = -1;
    if(!CHK_GXM(sceGxmCreateRenderTarget(&renderTargetParams, &renderTarget)))
        return false;

    // allocate memory and sync objects for display buffers
    uint32_t stride = ALIGN(winWidth, 64);
    displayBufferData.resize(GetNumColourBuffer());
    displaySurface.resize(GetNumColourBuffer());
    displayBufferSync.resize(GetNumColourBuffer());
    for(uint32_t i = 0; i < GetNumColourBuffer(); ++i)
    {
        // allocate memory for display
        radMemorySetAllocationName("displayBufferData");
        displayBufferData[i] = radMemorySpaceAllocAligned(
            radMemorySpace_Cdram,
            radMemoryGetCurrentAllocator(),
            4 * stride * winHeight,
            256);

        // memset the buffer to black
        for(uint32_t y = 0; y < winHeight; ++y) {
            uint32_t* row = (uint32_t*)displayBufferData[i] + y * stride;
            for(uint32_t x = 0; x < winWidth; ++x) {
                row[x] = 0xff000000;
            }
        }

        // initialize a color surface for this display buffer
        CHK_GXM(sceGxmColorSurfaceInit(
            &displaySurface[i],
            SCE_GXM_COLOR_FORMAT_A8B8G8R8,
            SCE_GXM_COLOR_SURFACE_LINEAR,
            (msaaMode == SCE_GXM_MULTISAMPLE_NONE) ? SCE_GXM_COLOR_SURFACE_SCALE_NONE : SCE_GXM_COLOR_SURFACE_SCALE_MSAA_DOWNSCALE,
            SCE_GXM_OUTPUT_REGISTER_SIZE_32BIT,
            winWidth,
            winHeight,
            stride,
            displayBufferData[i]));

        // create a sync object that we will associate with this buffer
        CHK_GXM(sceGxmSyncObjectCreate(&displayBufferSync[i]));
    }

    // compute the memory footprint of the depth buffer
    const uint32_t alignedWidth = ALIGN(GetWidth(), SCE_GXM_TILE_SIZEX);
    const uint32_t alignedHeight = ALIGN(GetHeight(), SCE_GXM_TILE_SIZEY);
    uint32_t sampleCount = alignedWidth * alignedHeight;
    uint32_t depthStrideInSamples = alignedWidth;
    if(msaaMode == SCE_GXM_MULTISAMPLE_4X) {
        // samples increase in X and Y
        sampleCount *= 4;
        depthStrideInSamples *= 2;
    }
    else if(msaaMode == SCE_GXM_MULTISAMPLE_2X) {
        // samples increase in Y only
        sampleCount *= 2;
    }

    // allocate it
    radMemorySetAllocationName("depthBufferData");
    depthBufferData = radMemorySpaceAllocAligned(
        radMemorySpace_Cdram,
        radMemoryGetCurrentAllocator(),
        4 * sampleCount,
        SCE_GXM_DEPTHSTENCIL_SURFACE_ALIGNMENT);

    // create the SceGxmDepthStencilSurface structure
    CHK_GXM(sceGxmDepthStencilSurfaceInit(
        &depthSurface,
        SCE_GXM_DEPTH_STENCIL_FORMAT_S8D24,
        SCE_GXM_DEPTH_STENCIL_SURFACE_TILED,
        depthStrideInSamples,
        depthBufferData,
        NULL));

    return true;
}

pddiDisplayInfo* gxmDisplay::GetDisplayInfo(void)
{
    return displayInfo;
}

unsigned gxmDisplay::GetFreeTextureMem()
{
    return unsigned(-1);
}

unsigned gxmDisplay::GetBufferMask()
{
    return unsigned(-1);
}

SceGxmMultisampleMode gxmDisplay::GetMSAAMode()
{
    return msaaMode;
}

int gxmDisplay::GetHeight()
{
    return winHeight;
}

int gxmDisplay::GetWidth()
{
    return winWidth;
}

int gxmDisplay::GetDepth()
{
    return winBitDepth;
}

pddiDisplayMode gxmDisplay::GetDisplayMode(void)
{
    return mode;
}

int gxmDisplay::GetNumColourBuffer(void)
{
    return kNumColourBuffers;
}

void gxmDisplay::GetGamma(float* r, float* g, float* b)
{
    *r = gammaR;
    *g = gammaG;
    *b = gammaB;
}

void gxmDisplay::SetGamma(float r, float g, float b)
{
    gammaR = r;
    gammaG = g;
    gammaB = b;

    Uint16 gamma[3][256];

    double igr = 1.0 / (double)r;
    double igg = 1.0 / (double)g;
    double igb = 1.0 / (double)b;

    const double n = 1.0 / 65535.0;

    for(int i=0; i < 256; i++)
    {
        double gcr = pow((double)initialGammaRamp[0][i] * n, igr);
        double gcg = pow((double)initialGammaRamp[1][i] * n, igg);
        double gcb = pow((double)initialGammaRamp[2][i] * n, igb);

        gamma[0][i] = (Uint16)(65535.0 * ((1.0 < gcr) ? 1.0 : gcr));
        gamma[1][i] = (Uint16)(65535.0 * ((1.0 < gcg) ? 1.0 : gcg));
        gamma[2][i] = (Uint16)(65535.0 * ((1.0 < gcb) ? 1.0 : gcb));
    }

#if SDL_MAJOR_VERSION < 3
    SDL_SetWindowGammaRamp(win, gamma[0], gamma[1], gamma[2]);
#endif
}

void gxmDisplay::SwapBuffers(void)
{
    // queue the display swap for this frame
    DisplayData displayData;
    displayData.address = displayBufferData[backBufferIndex];
    displayData.width = winWidth;
    displayData.height = winHeight;
    displayData.strideInPixels = ALIGN(winWidth, 64);
    sceGxmDisplayQueueAddEntry(
        displayBufferSync[frontBufferIndex],	// front buffer is OLD buffer
        displayBufferSync[backBufferIndex],		// back buffer is NEW buffer
        &displayData);

    // update buffer indices
    frontBufferIndex = backBufferIndex;
    backBufferIndex = (backBufferIndex + 1) % GetNumColourBuffer();
    reset = false;
}

    
unsigned gxmDisplay::Screenshot(pddiColour* buffer, int nBytes)
{
    // not implemented under vita
    assert( 0 && "PDDI: pddiDisplay::ScreenShot() - Not implemented under vita." );
    return 0;
}

void gxmDisplay::BeginTiming()
{
    beginTime = (float)SDL_GetTicks();
}

float gxmDisplay::EndTiming()
{
    return (float)SDL_GetTicks() - beginTime;
}

// How many vblanks of the 60 Hz screen this frame stays on it. 50 fps is not a whole number of
// vblanks: four frames of one and one of two make 5 frames per 6 vblanks.
static unsigned VblanksForNextFrame()
{
    static unsigned frame = 0;
    if(g_gxmFrameRate >= 60)
        return 1;
    if(g_gxmFrameRate >= 50)
        return (++frame % 5) == 0 ? 2 : 1;
    return 2;
}

void gxmDisplay::displayCallback(const void* callbackData)
{
    // Cast the parameters back
    const DisplayData* displayData = (const DisplayData*)callbackData;

    // Swap to the new buffer on the next VSYNC
    SceDisplayFrameBuf framebuf;
    memset(&framebuf, 0x00, sizeof(SceDisplayFrameBuf));
    framebuf.size = sizeof(SceDisplayFrameBuf);
    framebuf.base = displayData->address;
    framebuf.pitch = displayData->strideInPixels;
    framebuf.pixelformat = SCE_DISPLAY_PIXELFORMAT_A8B8G8R8;
    framebuf.width = displayData->width;
    framebuf.height = displayData->height;
    CHK_GXM(sceDisplaySetFrameBuf(&framebuf, SCE_DISPLAY_SETBUF_NEXTFRAME));

    // Block this callback until the swap has occurred and the old buffer
    // is no longer displayed. Waiting for 2 vblanks makes every frame stay on
    // screen for 33.3 ms: a locked 30 fps (the rate the game was made for) instead
    // of 60/45/30 jumping when the frame takes 17-25 ms at 333 MHz. The interval
    // comes from "fps=" in vita.cfg (win32main.cpp).
    const unsigned vblanks = VblanksForNextFrame();
    if(vblanks > 1)
    {
        CHK_GXM(sceDisplayWaitVblankStartMulti(vblanks));
    }
    else
    {
        CHK_GXM(sceDisplayWaitVblankStart());
    }
}