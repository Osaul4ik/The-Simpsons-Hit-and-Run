//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        memoryspacewin32.cpp
//
// Subsystem:	Foundation Technologies - Memory Operation Services
//
// Description:	This file contains the win 32implementation of the 
//              remote memory space operations
//
// Revisions:   13-Oct-2001     Creation     Rob
//
//=============================================================================

#include "pch.hpp"
#include <radmemory.hpp>
#include <string.h>
#include <radmemorymonitor.hpp>

#ifdef RAD_VITA
#include <psp2/ctrl.h>
#include <psp2/kernel/sysmem.h>
#endif

#include "memoryspacewin32.hpp"

#ifdef RAD_VITA
#ifdef RAD_GXM
// Native GXM backend: textures, display/depth buffers and CDRAM-resident data live in
// radMemorySpace_Cdram (all the VRAM left after the GXM driver has initialised, except
// RAD_VITA_GXM_CDRAM_RESERVE_KB of slack; the driver itself is initialised first), and vertex data, ring buffers and the
// overflow of textures live in radMemorySpace_User (GPU-readable RAM).
// vitaGL is not linked in this configuration, so nothing else competes for this memory.
#ifndef RAD_VITA_GXM_USER_MB
#define RAD_VITA_GXM_USER_MB 80
#endif
#ifndef RAD_VITA_GXM_CDRAM_RESERVE_KB
#define RAD_VITA_GXM_CDRAM_RESERVE_KB 2048
#endif
#define USER_SIZE_IN_BYTES ( 1024 * 1024 * RAD_VITA_GXM_USER_MB )
#define CDRAM_SIZE_IN_BYTES ( 1024 * 1024 * 16 ) // fallback if the free size cannot be queried
#else
// vitaGL builds: nothing allocates from radMemorySpace_User/Cdram, and vitaGL needs
// this RAM/CDRAM, so keep these blocks tiny.
#define USER_SIZE_IN_BYTES ( 1024 * 1024 * 1 )
#define CDRAM_SIZE_IN_BYTES ( 1024 * 1024 * 1 )
#endif
#else
#define USER_SIZE_IN_BYTES ( 1024 * 1024 * 1 )
#define CDRAM_SIZE_IN_BYTES ( 1024 * 1024 * 1 )
#endif
#define MAX_USER_ALLOCATIONS 100
#define MAX_CDRAM_ALLOCATIONS 100

//============================================================================
//
// Global Functions
//
//============================================================================

#ifdef RAD_VITA
#ifdef RAD_GXM
// Defined in libs/pure3d/pddi/gxm/display.cpp. Weak so radcore still links when pure3d
// is not part of the link.
extern "C" bool radGxmEarlyInit( void ) __attribute__((weak));
#endif
SceUID g_User_UID, g_Cdram_UID;
IRadMemoryHeap * g_pIRadMemoryHeap_User = NULL;
IRadMemoryHeap * g_pIRadMemoryHeap_Cdram = NULL;
#endif

// XBox doesn't have any optimal memory space alignment or multiple requirements

unsigned int radMemorySpace_OptimalMultiple = 4;
unsigned int radMemorySpace_OptimalAlignment = 4;

// Only need one object global object because win32/xbox copies are synchronous.

MemorySpaceAsyncRequest_Copy g_MemorySpaceAsyncRequest_Copy_Placeholder;

#ifdef RAD_VITA
// Allocates a memblock of up to 'size' bytes. If the kernel refuses, retries with
// smaller sizes (in 'step' decrements) down to 'minSize' so that a slightly different
// memory layout degrades gracefully instead of crashing at boot.
static SceUID AllocBlockWithFallback( const char* name, SceKernelMemBlockType type,
                                      unsigned int* size, unsigned int step, unsigned int minSize )
{
    for( ;; )
    {
        SceUID uid = sceKernelAllocMemBlock( name, type, *size, NULL );
        if( uid >= SCE_OK )
        {
            return uid;
        }
        if( *size <= minSize + step )
        {
            return uid;
        }
        *size -= step;
    }
}
#endif

void radMemorySpaceInitialize( void )
{
#ifdef RAD_VITA
    int err = SCE_OK;
    void* mem = NULL;

    unsigned int userSize = USER_SIZE_IN_BYTES;
    unsigned int cdramSize = CDRAM_SIZE_IN_BYTES;

#ifdef RAD_GXM
    // Initialise the GXM driver BEFORE the game heaps below take nearly all user RAM and
    // CDRAM: sceGxmInitialize() needs its own driver memory and otherwise fails with
    // SCE_GXM_ERROR_OUT_OF_MEMORY (0x805B0002). Failure is not fatal here, InitDisplay()
    // retries and reports it.
    if( radGxmEarlyInit != NULL )
    {
        bool ok = radGxmEarlyInit();
#ifdef RAD_LOG
        rReleasePrintf( "[mem] early GXM init: %s\n", ok ? "ok" : "FAILED" );
#endif
        (void)ok;
    }
    {
        SceKernelFreeMemorySizeInfo info;
        info.size = sizeof( info );
        if( sceKernelGetFreeMemorySize( &info ) >= 0 )
        {
            const unsigned int reserve = RAD_VITA_GXM_CDRAM_RESERVE_KB * 1024;
            if( info.size_cdram > reserve )
            {
                // CDRAM memblocks must be a multiple of 256 KB
                cdramSize = ( info.size_cdram - reserve ) & ~( 256 * 1024 - 1 );
            }
        }
    }
#endif

    g_User_UID = AllocBlockWithFallback( "user", SCE_KERNEL_MEMBLOCK_TYPE_USER_RW_UNCACHE,
                                         &userSize, 1024 * 1024, 1024 * 1024 );
    rAssert(g_User_UID >= SCE_OK);

    err = sceKernelGetMemBlockBase(g_User_UID, &mem);
    rAssert(err == SCE_OK);

    g_pIRadMemoryHeap_User = radMemoryCreateDougLeaHeap(
        mem, userSize, RADMEMORY_ALLOC_DEFAULT, "user");

    g_Cdram_UID = AllocBlockWithFallback( "cdram", SCE_KERNEL_MEMBLOCK_TYPE_USER_CDRAM_RW,
                                          &cdramSize, 1024 * 1024, 1024 * 1024 );
    rAssert(g_Cdram_UID >= SCE_OK);

    err = sceKernelGetMemBlockBase(g_Cdram_UID, &mem);
    rAssert(err == SCE_OK);

    g_pIRadMemoryHeap_Cdram = radMemoryCreateDougLeaHeap(
        mem, cdramSize, RADMEMORY_ALLOC_DEFAULT, "cdram");

#ifdef RAD_LOG
    rReleasePrintf( "[mem] radMemorySpace: user=%u KB cdram=%u KB\n", userSize / 1024, cdramSize / 1024 );
#endif
#endif
}

void radMemorySpaceTerminate( void )
{
#ifdef RAD_VITA
    int err = SCE_OK;
    void* mem = NULL;

    rAssert( g_pIRadMemoryHeap_User != NULL );
    rAssert( g_pIRadMemoryHeap_Cdram != NULL );

    g_pIRadMemoryHeap_User->Release();
    g_pIRadMemoryHeap_User = NULL;

    g_pIRadMemoryHeap_Cdram->Release();
    g_pIRadMemoryHeap_Cdram = NULL;

    err = sceKernelFreeMemBlock(g_User_UID);
    rAssert(err == SCE_OK);

    err = sceKernelFreeMemBlock(g_Cdram_UID);
    rAssert(err == SCE_OK);
#endif
}

#if defined( RAD_VITA ) && defined( RAD_GXM )
// Texture memory that does not fit in CDRAM spills into the (GPU-readable) user block.
static bool IsInUserBlock( void* p )
{
    char* start = (char*)g_pIRadMemoryHeap_User->GetStartOfMemory();
    return (char*)p >= start && (char*)p < start + g_pIRadMemoryHeap_User->GetSize();
}
#endif

//============================================================================
// radMemoryCopyAsync
//============================================================================

IRadMemorySpaceCopyRequest * radMemorySpaceCopyAsync
(
	void * pDest,
	radMemorySpace spaceDest,
	const void * pSrc,
	radMemorySpace spaceSrc,
	unsigned int bytes
)
{   
    rAssert( pDest != NULL );
    rAssert( spaceDest = radMemorySpace_Main );
    rAssert( pSrc != NULL );
    rAssert( spaceSrc = radMemorySpace_Main );
    rAssert( bytes > 0 );

	::memcpy( pDest, pSrc, bytes ); 

	return & g_MemorySpaceAsyncRequest_Copy_Placeholder;
}

MemorySpaceAsyncRequest_Copy::MemorySpaceAsyncRequest_Copy( void )
    :
    m_RefCount( 0 )
{
    radMemoryMonitorIdentifyAllocation( this, g_nameFTech, "MemorySpaceAsyncRequest_Copy" );
}

MemorySpaceAsyncRequest_Copy::~MemorySpaceAsyncRequest_Copy( void )
{
    rAssert( m_RefCount == 0 ); // The game is not reference counting properly
}

void MemorySpaceAsyncRequest_Copy::AddRef( void )
{
    m_RefCount++;
}

void MemorySpaceAsyncRequest_Copy::Release( void )
{
    rAssert( m_RefCount > 0 );
    m_RefCount--;
}

bool MemorySpaceAsyncRequest_Copy::IsDone( void )
{
    return true;
}

//============================================================================
// radMemoryAlloc
//============================================================================

void * radMemorySpaceAlloc( radMemorySpace space, radMemoryAllocator allocator, unsigned int numBytes )
{
#ifdef RAD_VITA
    if(space == radMemorySpace_User)
    {
        return g_pIRadMemoryHeap_User->GetMemory( numBytes );
    }
    else if(space == radMemorySpace_Cdram)
    {
        void* p = g_pIRadMemoryHeap_Cdram->GetMemory( numBytes );
#ifdef RAD_GXM
        if( p == NULL )
        {
            p = g_pIRadMemoryHeap_User->GetMemory( numBytes );
        }
#endif
        return p;
    }
#endif
	return ::radMemoryAlloc( allocator, numBytes );
}

//============================================================================
// radMemoryAllocAligned
//============================================================================

void * radMemorySpaceAllocAligned
( 
	radMemorySpace space, 
	radMemoryAllocator allocator,
	unsigned int numBytes, 
	unsigned int alignment 
)
{
#ifdef RAD_VITA
    if(space == radMemorySpace_User)
    {
        return g_pIRadMemoryHeap_User->GetMemoryAligned( numBytes, alignment );
    }
    else if(space == radMemorySpace_Cdram)
    {
        void* p = g_pIRadMemoryHeap_Cdram->GetMemoryAligned( numBytes, alignment );
#ifdef RAD_GXM
        if( p == NULL )
        {
            p = g_pIRadMemoryHeap_User->GetMemoryAligned( numBytes, alignment );
        }
#endif
        return p;
    }
#endif
	return ::radMemoryAllocAligned( allocator, numBytes, alignment );
}

//============================================================================
// radMemorySpaceGetAllocator
//============================================================================

IRadMemoryAllocator * radMemorySpaceGetAllocator( radMemorySpace memSpace, radMemoryAllocator allocator )
{
#ifdef RAD_VITA
    if(memSpace == radMemorySpace_User)
    {
        return g_pIRadMemoryHeap_User;
    }
    else if(memSpace == radMemorySpace_Cdram)
    {
        return g_pIRadMemoryHeap_Cdram;
    }
#endif
    rAssert( memSpace == radMemorySpace_Main );

    return ::radMemoryGetAllocator( allocator );
}

//============================================================================
// radMemoryFree
//============================================================================

void radMemorySpaceFree
(  
	radMemorySpace space, 
	radMemoryAllocator allocator,
	void * pMemory
)
{
#ifdef RAD_VITA
    if(space == radMemorySpace_User)
    {
        return g_pIRadMemoryHeap_User->FreeMemory( pMemory );
    }
    else if(space == radMemorySpace_Cdram)
    {
#ifdef RAD_GXM
        if( pMemory != NULL && IsInUserBlock( pMemory ) )
        {
            return g_pIRadMemoryHeap_User->FreeMemory( pMemory );
        }
#endif
        return g_pIRadMemoryHeap_Cdram->FreeMemory( pMemory );
    }
#endif
	::radMemoryFree( allocator, pMemory );
}

//============================================================================
// radMemoryFreeAligned
//============================================================================

void radMemorySpaceFreeAligned
( 
	radMemorySpace space, 
	radMemoryAllocator allocator,
	void * pMemory
)
{
#ifdef RAD_VITA
    if(space == radMemorySpace_User)
    {
        return g_pIRadMemoryHeap_User->FreeMemoryAligned( pMemory );
    }
    else if(space == radMemorySpace_Cdram)
    {
#ifdef RAD_GXM
        if( pMemory != NULL && IsInUserBlock( pMemory ) )
        {
            return g_pIRadMemoryHeap_User->FreeMemoryAligned( pMemory );
        }
#endif
        return g_pIRadMemoryHeap_Cdram->FreeMemoryAligned( pMemory );
    }
#endif
	::radMemoryFreeAligned( allocator, pMemory );
}