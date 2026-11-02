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
#define PRIM_POOL_DEPTH     4
#define PRIM_POOL_MAX_VERTS 256   // only small buffers are pooled, to bound the memory held

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

    static void* patcherHostAlloc(void* userData, uint32_t size);
    static void patcherHostFree(void* userData, void* mem);

    gxmDevice* device;
    gxmDisplay* display;
    struct SceGxmContext* context;

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

    // Pool of finished immediate-mode prim buffers, keyed by (type, format, vertex count).
    // Creating one costs three heap allocations plus a precomputed-draw init, which was
    // done for every BeginPrims() call.
    class gxmPrimBuffer* primPool[PRIM_POOL_BUCKETS][PRIM_POOL_DEPTH];
    unsigned char primPoolCount[PRIM_POOL_BUCKETS];
    unsigned primPoolHits, primPoolMisses, midSceneFlushes, statFrames;

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
    bool Matches(pddiPrimType type, unsigned vertexFormat, int nVertex) const
    {
        return primType == type && vertexType == vertexFormat && (int)allocated == nVertex;
    }
    static unsigned PoolBucket(pddiPrimType type, unsigned vertexFormat, unsigned nVertex)
    {
        unsigned k = nVertex * 2654435761u ^ vertexFormat * 40503u ^ (unsigned)type * 97u;
        return (k ^ (k >> 7) ^ (k >> 15)) % PRIM_POOL_BUCKETS;
    }
    unsigned PoolBucket() const { return PoolBucket(primType, vertexType, allocated); }

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

    bool valid;
    SceGxmVertexProgram* vertexShader;
    SceGxmPrecomputedDraw precomputed;
    unsigned char* state;

    unsigned mem;
};
    
#endif