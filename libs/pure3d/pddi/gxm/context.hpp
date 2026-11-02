//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#ifndef _CONTEXT_HPP_
#define _CONTEXT_HPP_

// Max immediate-mode draws in flight per scene. When this ring fills up, BeginPrims() has to
// do a mid-scene flush and wait for the GPU, which stalls the CPU and costs an extra
// tile pass (typical cause of 60 -> 30 fps drops in dense areas). Entries are just
// pointers, so a large ring is cheap.
#define BUFFERED_VERTS 4096

// Recycling of immediate-mode prim buffers (see gxmContext::AcquirePrimBuffer).
#define PRIM_POOL_BUCKETS   64
#define PRIM_POOL_DEPTH     512       // per bucket: a frame issues hundreds of same-sized draws and buffers
                                      // only come back once the GPU has finished with them
#define PRIM_POOL_MAX_BYTES (8 * 1024 * 1024)   // total memory the pool may hold, across all buckets
#define PRIM_POOL_MAX_VERTS 512   // only small buffers are pooled, to bound the memory held
#define PRIM_POOL_MIN_VERTS 8     // buffer capacities are rounded up to a power of two >= this

#include <pddi/pddi.hpp>
#include <pddi/base/basecontext.hpp>

#include <map>
#include <vector>

class gxmDisplay;
class gxmDevice;
class gxmProgram;
class gxmTextureEnv;

class gxmExtGamma;

//--------------------------------------------------------------
class gxmContext : public pddiBaseContext
{
public :
    gxmContext(gxmDevice* dev, gxmDisplay* disp);
    ~gxmContext();

    // frame synchronisation
    void BeginFrame();
    void EndFrame();

    // Wait until the GPU has finished every submitted frame (and the display queue is empty).
    // Must be called before freeing textures / vertex buffers / shaders that a frame still in
    // flight may reference. The GXM backend never implemented it (the base class is a no-op),
    // so the GUI's "p3d::pddi->DrawSync(); remove from inventory" pattern freed GPU memory under
    // the GPU. Does nothing between BeginFrame() and EndFrame() (sceGxmFinish is illegal in a scene).
    void DrawSync();

    // buffer clearing
    void Clear(unsigned bufferMask);

    // viewport clipping
    void SetScissor(pddiRect* rect);

    // immediate mode prim rendering
    pddiPrimStream* BeginPrims(pddiShader* material, pddiPrimType primType, unsigned vertexType, int vertexCount, unsigned pass = 0);
    void EndPrims(pddiPrimStream* stream);

    // retained mode prim rendering
    void DrawPrimBuffer(pddiShader* material, pddiPrimBuffer* buffer);

    // lighting
    int GetMaxLights();
    void SetAmbientLight(pddiColour col);

    // backface culling
    void EnableCulling(bool enable);
    void SetCullMode(pddiCullMode mode);

    // colour buffer control
    void SetColourWrite( bool red, bool green, bool blue, bool alpha );

    // Current colour write mask as PDDI_WRITE_* bits. GXM bakes the mask into the fragment
    // program's blend state, so gxmMat combines this with its own mask when it picks a program.
    int GetColourWriteMask() const;

    // z-buffer control
    void EnableZBuffer(bool enable);
    void SetZCompare(pddiCompareMode compareMode);
    void SetZWrite(bool);
    void SetZBias(float bias);
    void SetZRange(float n, float f);

    // stencil buffer control
    void EnableStencilBuffer(bool enable);
    void SetStencilCompare(pddiCompareMode compare);
    void SetStencilRef(int ref);
    void SetStencilMask(unsigned mask);
    void SetStencilWriteMask(unsigned mask);
    void SetStencilOp(pddiStencilOp failOp, pddiStencilOp zFailOp, pddiStencilOp zPassOp);

    // polygon fill
    void SetFillMode(pddiFillMode mode);

    // fog
    void EnableFog(bool enable);
    void SetFog(pddiColour colour, float start, float end);

    // utility
    int GetMaxTextureDimension(void);

    // extensions
    pddiExtension* GetExtension(unsigned extID);
    bool VerifyExtension(unsigned extID);

    // internal pddiglfunctions
    gxmDisplay* GetDisplay(void) {return display;}
    gxmProgram* GetFragmentProgram(const gxmTextureEnv* texEnv);
    void SetTextureEnvironment(const gxmTextureEnv* texEnv, gxmProgram* fragProgram);

    unsigned contextID;

protected:
    friend class gxmPrimBuffer;
    void LoadHardwareMatrix(pddiMatrixType id);
    void SetupHardwareProjection(void);
    void SetupHardwareStencil(void);
    void SetupHardwareLight(int);
    void  BeginTiming(void);
    float EndTiming(void);

    void SetVertexShader(SceGxmVertexProgram* vert);
    void FlushVertexStreams();

    // Programs the hardware region clip from the current scissor rect. Only valid inside a
    // scene; BeginFrame() re-applies it because the scissor can be set between scenes.
    void ApplyScissor();

    static void* patcherHostAlloc(void* userData, uint32_t size);
    static void patcherHostFree(void* userData, void* mem);

    gxmDevice* device;
    gxmDisplay* display;
    struct SceGxmContext* context;
    bool inScene;               // true between sceGxmBeginScene and sceGxmEndScene

    gxmExtGamma* extGamma;

    pddiShader* defaultShader;
    pddiShader* clearShader;
    pddiVector4* dummyVector;

    gxmProgram* vertexProgram;
    gxmProgram* colorProgram;
    gxmProgram* textureProgram;
    gxmProgram* alphaTestProgram;

    SceGxmVertexProgram* vertexShader;
    void* vertexUniformBuffer;
    void* fragmentUniformBuffer;

    SceUID patcherVertexUsseUid;
    SceUID patcherFragmentUsseUid;
    void* patcherBuffer;
    SceGxmShaderPatcher* shaderPatcher;

//   int nBuffered;
//   unsigned currentMatId;
//   pddiScreenVertex buffer[BUFFERED_VERTS];

// void FlushBuffer(void);
// BOOL ZTestQuery(float x, float y, float z);
// void ZTestPoint(float x, float y, float z);
//   pddiFillMode fillMode;

//   int yRes;

//   ULONG compareMode;
//   BOOL zWrite;
//   float alphaRef;

    pddiMatrix projection;

    // Circular vertex stream buffer array
    unsigned int streamsTail;
    SceGxmNotification streamsHead;
    class pddiPrimBuffer* streams[BUFFERED_VERTS];

    // Pool of finished immediate-mode prim buffers, keyed by (type, format, capacity).
    // Capacity is the vertex count rounded up to a power of two, so buffers are reused for
    // any count that fits; the draw is then limited to the vertices actually written.
    // Creating one costs three heap allocations plus a precomputed-draw init, which was
    // done for every BeginPrims() call.
    class gxmPrimBuffer* primPool[PRIM_POOL_BUCKETS][PRIM_POOL_DEPTH];
    unsigned short primPoolCount[PRIM_POOL_BUCKETS];   // depth can exceed 255
    unsigned primPoolBytes;     // memory currently held by pooled buffers
    unsigned primPoolHits, primPoolMisses, midSceneFlushes, statFrames;

    static int PrimPoolCapacity(pddiPrimType type, int nVertex);
    class gxmPrimBuffer* AcquirePrimBuffer(pddiPrimType type, unsigned vertexType, int nVertex);
    void RecyclePrimBuffer(pddiPrimBuffer* buffer);
    void DrainPrimPool();

    // Reserve per-draw default uniform buffers. If a ring buffer is exhausted mid-scene
    // (SCE_GXM_ERROR_RESERVE_FAILED) flush the scene, wait for the GPU and retry once
    // instead of silently dropping the draw.
    void MidSceneFlushAndWait();
    bool ReserveVertexUniforms();
    bool ReserveFragmentUniforms();
};

class gxmPrimBufferStream;

class gxmPrimBuffer : public pddiPrimBuffer
{
public:
    gxmPrimBuffer(gxmContext* context, pddiPrimType type, unsigned vertexFormat, int nVertex, int nIndex);
    ~gxmPrimBuffer();

    pddiPrimBufferStream* Lock();
    void Unlock(pddiPrimBufferStream* stream);

    unsigned char* LockIndexBuffer();
    void UnlockIndexBuffer(int count);

    void SetIndices(unsigned short* indices, int count);

    bool CheckMemImageVersion(int version) { return false; }
    void* LockMemImage(unsigned) { return NULL;}
    void UnlockMemImage() { }
    unsigned GetMemImageLength() {return 0; }
    void SetMemImageParam(unsigned param, unsigned value) { /**/ }

    SceGxmVertexProgram* GetVertexShader() { return vertexShader; }
    unsigned GetVertexFormat() { return vertexType; }
    unsigned GetStride() { return stride; }
    void Display(void);

    // pool support
    bool IsUnshared() const { return refCount <= 0; }
    bool Poolable() const { return allocated <= PRIM_POOL_MAX_VERTS && indexCount == allocated; }
    bool Matches(pddiPrimType type, unsigned vertexFormat, int capacity) const
    {
        return primType == type && vertexType == vertexFormat && (int)allocated == capacity;
    }
    // Limits the draw to the first nVertex vertices (identity index buffer). Only valid for
    // poolable buffers, whose index count equals their capacity.
    void SetDrawCount(unsigned nVertex);
    static unsigned PoolBucket(pddiPrimType type, unsigned vertexFormat, unsigned nVertex)
    {
        unsigned k = nVertex * 2654435761u ^ vertexFormat * 40503u ^ (unsigned)type * 97u;
        return (k ^ (k >> 7) ^ (k >> 15)) % PRIM_POOL_BUCKETS;
    }
    unsigned PoolBucket() const { return PoolBucket(primType, vertexType, allocated); }
    unsigned PoolBytes() const { return mem + indexCount * sizeof(unsigned short) + stateSize; }

protected:
    friend class gxmPrimBufferStream;
    gxmPrimBufferStream* stream;
    gxmContext* context;

    pddiPrimType primType;
    unsigned vertexType;

    int nStrips;
    int* strips;

    unsigned char* buffer;
    float* coord;
    float* normal;
    float* uv;
    unsigned char* colour;

    unsigned allocated;
    unsigned total;
    unsigned stride;

    unsigned short* indices;
    unsigned indexCount;
    unsigned drawCount;     // indices submitted by Display(); <= indexCount

    bool valid;
    SceGxmVertexProgram* vertexShader;
    SceGxmPrecomputedDraw precomputed;
    unsigned char* state;

    unsigned mem;
    unsigned stateSize;     // size of the precomputed draw state allocation
};
    
#endif