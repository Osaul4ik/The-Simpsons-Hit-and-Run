//=============================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// File:        xboxmain.cpp
//
// Description: This file contains the main enrty point to the game.
//
// History:     + Based on xbox main and winmain from squidney
//
//=============================================================================

//========================================
// System Includes
//========================================
// Standard Library
#include <string.h>
// Foundation Tech
#include <raddebug.hpp>
#include <radobject.hpp>
// file reading before radtech
#include <stdio.h>
#include <SDL_main.h>

#ifdef __SWITCH__
#include <switch.h>
#endif

#ifdef RAD_VITA
#include <unistd.h>
#include <malloc.h>
#include <psp2/kernel/clib.h>
#include <psp2/kernel/sysmem.h>
#include <psp2/kernel/threadmgr.h>
#include <radmemory.hpp>
unsigned int sceLibcHeapSize = 16 * 1024 * 1024;

// Size of the newlib heap backing malloc/new, read by vitasdk's sbrk at startup.
// The app has 368 MB of user memory in extended memory mode (ATTRIBUTE2=12, see
// CMakeLists.txt). The heap gets what is left after the other fixed user blocks
// (radMemorySpace user block, 16 MB sceLibc heap, executable, thread stacks and
// system modules), so keep it well below 368 MB. See the memory budget in CMakeLists.txt.
// With the vitaGL backend, vitaGL takes all the remaining user RAM (minus its 16 MB
// threshold) for its texture pool when vglInit() runs, so leave at least ~100 MB free
// there or the screen stays black. The GXM backend does not do that: its memory is
// the radMemorySpace user and CDRAM blocks (libs/radcore/.../memoryspacewin32.cpp).
#ifndef RAD_VITA_HEAP_MB
#define RAD_VITA_HEAP_MB 252
#endif
unsigned int _newlib_heap_size_user = RAD_VITA_HEAP_MB * 1024 * 1024;
#endif

//========================================
// Project Includes
//========================================
#include <main/game.h>
#include <main/win32platform.h>
#include <main/singletons.h>
#include <main/commandlineoptions.h>
#include <memory/memoryutilities.h>
#include <memory/srrmemory.h>
#include <p3d/entity.hpp>
#ifdef RAD_VITA
#include <render/DSG/IEntityDSG.h>
#endif

//========================================
// Forward Declarations
//========================================
static void ProcessCommandLineArguments( int argc, char *argv[] );

static void ProcessCommandLineArgumentsFromFile();

//=============================================================================
// Runtime settings (Vita). Read from ux0:data/simpsons/vita.cfg, one "key=value"
// per line, so they can be tuned without rebuilding:
//     fps=30            30 (default) or 60; also chosen in the game's Options menu,
//                       which rewrites this line
//     zonepreload=1     all the outside zones of a level load behind the loading screen and stay, so
//                       nothing streams while driving; 0 = they load on the way, the level opens faster
//     zonecache=8       max zones kept resident after leaving them (0 = off)
//     zonefade=600      milliseconds over which what a zone adds fades in, 0 = it just appears
//     smalldist=100     metres from which small things (hydrants, ...) are not drawn; the original
//                       game has 63, higher costs more draw calls
//     landmark=30       things with a bounding sphere at least this big in metres are drawn from any
//                       distance, so that the skyline does not appear at 200 m; 0 = off
//     cullmargin=4      metres outside the view at which things are still drawn, so that a street
//                       lamp and its light do not go out on screen; more costs draw calls
//     framepace=1       0 = the game simulates the time the clock shows instead of the time the
//                       frames stay on screen
//     smoothload=1      0 = the loader always gets 4 ms per frame during play, instead of what
//                       the frame has left
//     log=0             1 = write ux0:data/simpsons/log.txt; 0 = no log and no timing for it
//     prof=0            1 = also time every mark and add the [prof] table to the log (costs
//                       about 2 ms per frame, so leave it off when judging the frame rate)
//     soundthread=1     0 = service the sound on the main thread
//     smokebudget=25    how many whole screens the particles of a frame may cover, in tenths;
//                       lower = smoke costs less and thins out sooner, 0 = no limit and no
//                       fading near the camera
//     pixcull=4         solid things with a smaller radius on screen, in pixels, are not drawn; 0 = all
//     occlsort=1        0 = solid things are sorted by material alone, not near ones first
//     occlusion=1       0 = no hardware visibility queries: objects hidden behind others are drawn too
//     framecull=1       0 = things above and below the picture are drawn too, as in the original
//     skylineall=0      1 = the big landmarks of zones asleep in memory are drawn too, for more
//                       draw calls
//     npcfar=1          0 = pedestrians past 25 m run their actions every frame, not every 4th
//     pedlimit=7        most pedestrians at once, 0 to 7; fewer costs less and shows fewer people
//     trilinear=1       0 = nearest mip level instead of blending between levels
//     fastshaders=1     0 = only the packaged shaders, no runtime compiled variants
//     hwskin=1          0 = skin on the CPU (NEON) even when the shaders compiled
// Missing file or key = compile-time default.
//=============================================================================
#ifdef RAD_VITA
#include <psp2/power.h>
#ifndef RAD_VITA_ZONE_CACHE_MAX_ZONES
#define RAD_VITA_ZONE_CACHE_MAX_ZONES 8
#endif
#ifndef RAD_VITA_FRAME_RATE
#define RAD_VITA_FRAME_RATE 30            // 30 or 60
#endif
extern int g_vitaZonePreload;                                  // render/RenderManager/RenderManager.cpp
int g_vitaZoneCacheMaxZones = RAD_VITA_ZONE_CACHE_MAX_ZONES;   // read by RenderManager
int g_gxmFrameRate          = RAD_VITA_FRAME_RATE;             // read by gxmDisplay::displayCallback
#ifdef RAD_LOG
extern int g_vitaProfEnabled;                                  // code/debug/vitaprof.cpp
#endif
extern int g_vitaPixCull;                                      // code/render/Culling/WorldScene.cpp
extern bool g_vitaOcclSort;                                    // code/render/Culling/WorldScene.cpp
extern bool g_vitaOcclusion;                                   // code/render/Culling/WorldScene.cpp
extern bool g_vitaFrameCull;                                   // code/render/Culling/WorldScene.cpp
extern bool g_vitaSkylineAll;                                  // code/render/RenderManager/WorldRenderLayer.cpp
extern bool g_vitaNpcFarCadence;                               // code/worldsim/character/character.cpp
extern int g_vitaPedLimit;                                     // code/worldsim/ped/pedestrianmanager.cpp
extern float g_vitaSmallThingDist;                             // code/render/Culling/WorldScene.cpp
extern float g_vitaParticleBudget;                             // p3d/effects/particleemitter.cpp
extern float g_vitaLandmarkRadius;                             // render/Culling/WorldScene.cpp
extern float g_vitaCullMargin;                                 // render/Culling/WorldScene.cpp
int g_vitaFramePace         = 1;                               // read by Game::Run
int g_vitaSmoothLoad        = 1;                               // read by Game::Run
extern int g_vitaSoundWorker;                                  // code/sound/soundmanager.cpp
static int s_vitaProf       = 0;
int g_gxmTrilinear          = 1;                               // read by gxmMat::SetDevPass
int g_gxmFastShaders        = 1;                               // read by gxmContext::LoadShaderVariants
int g_gxmHardwareSkinning   = 1;                               // read by gxmContext::LoadShaderVariants

// Set once at startup, whatever was set before the game started. The game does not change them
// after that, so a clock set from outside while it runs stays.
static void SetVitaClocks()
{
    const int cpu = scePowerSetArmClockFrequency( 333 );
    const int gpu = scePowerSetGpuClockFrequency( 166 );
    const int bus = scePowerSetBusClockFrequency( 222 );
    const int xbar = scePowerSetGpuXbarClockFrequency( 166 );
    rReleasePrintf( "[boot] clocks: CPU %d MHz (0x%08x), GPU %d MHz (0x%08x), bus %d MHz (0x%08x), GPU xbar %d MHz (0x%08x)\n",
                    scePowerGetArmClockFrequency(), cpu, scePowerGetGpuClockFrequency(), gpu,
                    scePowerGetBusClockFrequency(), bus, scePowerGetGpuXbarClockFrequency(), xbar );
}

void VitaSetFrameRate( int fps );

static void LoadVitaConfig()
{
    g_radLogEnabled = 0;    // no line in the file, no log
    FILE* f = fopen( "ux0:data/simpsons/vita.cfg", "r" );
    if( f == NULL )
    {
        return;
    }
    char line[96];
    while( fgets( line, sizeof( line ), f ) != NULL )
    {
        int v = 0;
        if( sscanf( line, "fps=%d", &v ) == 1 )
        {
            VitaSetFrameRate( v );
        }
        else if( sscanf( line, "log=%d", &v ) == 1 )
        {
            g_radLogEnabled = v != 0;
        }
        else if( sscanf( line, "prof=%d", &v ) == 1 )
        {
            s_vitaProf = v != 0;
        }
        else if( sscanf( line, "zonefade=%d", &v ) == 1 )
        {
            IEntityDSG::msFadeMs = v > 0 ? (unsigned int)v : 0;
        }
        else if( sscanf( line, "landmark=%d", &v ) == 1 )
        {
            g_vitaLandmarkRadius = (float)( v > 0 ? v : 0 );
        }
        else if( sscanf( line, "cullmargin=%d", &v ) == 1 )
        {
            g_vitaCullMargin = (float)( v > 0 ? v : 0 );
        }
        else if( sscanf( line, "smalldist=%d", &v ) == 1 )
        {
            g_vitaSmallThingDist = (float)v;
        }
        else if( sscanf( line, "framepace=%d", &v ) == 1 )
        {
            g_vitaFramePace = v != 0;
        }
        else if( sscanf( line, "smoothload=%d", &v ) == 1 )
        {
            g_vitaSmoothLoad = v != 0;
        }
        else if( sscanf( line, "smokebudget=%d", &v ) == 1 )
        {
            g_vitaParticleBudget = (float)v * 0.1f;
        }
        else if( sscanf( line, "pixcull=%d", &v ) == 1 )
        {
            g_vitaPixCull = v < 0 ? 0 : v;
        }
        else if( sscanf( line, "occlsort=%d", &v ) == 1 )
        {
            g_vitaOcclSort = v != 0;
        }
        else if( sscanf( line, "occlusion=%d", &v ) == 1 )
        {
            g_vitaOcclusion = v != 0;
        }
        else if( sscanf( line, "framecull=%d", &v ) == 1 )
        {
            g_vitaFrameCull = v != 0;
        }
        else if( sscanf( line, "skylineall=%d", &v ) == 1 )
        {
            g_vitaSkylineAll = v != 0;
        }
        else if( sscanf( line, "npcfar=%d", &v ) == 1 )
        {
            g_vitaNpcFarCadence = v != 0;
        }
        else if( sscanf( line, "pedlimit=%d", &v ) == 1 )
        {
            g_vitaPedLimit = v < 0 ? 0 : ( v > 7 ? 7 : v );
        }
        else if( sscanf( line, "soundthread=%d", &v ) == 1 )
        {
            g_vitaSoundWorker = v != 0;
        }
        else if( sscanf( line, "trilinear=%d", &v ) == 1 )
        {
            g_gxmTrilinear = v != 0;
        }
        else if( sscanf( line, "fastshaders=%d", &v ) == 1 )
        {
            g_gxmFastShaders = v != 0;
        }
        else if( sscanf( line, "hwskin=%d", &v ) == 1 )
        {
            g_gxmHardwareSkinning = v != 0;
        }
        else if( sscanf( line, "zonepreload=%d", &v ) == 1 )
        {
            g_vitaZonePreload = v != 0;
        }
        else if( sscanf( line, "zonecache=%d", &v ) == 1 )
        {
            g_vitaZoneCacheMaxZones = ( v < 0 ) ? 0 : ( v > 32 ? 32 : v );
        }
    }
    fclose( f );
}

// Frame rate choice of the options menu. Takes effect on the next frame.
int VitaGetFrameRate()
{
    return g_gxmFrameRate;
}

void VitaSetFrameRate( int fps )
{
    g_gxmFrameRate = ( fps >= 60 ) ? 60 : 30;
}

// Writes the current frame rate to vita.cfg, keeping every other line of the file.
// A file that does not fit the buffer is left alone (the choice then lasts until the game exits).
void VitaSaveFrameRate()
{
    const char* path = "ux0:data/simpsons/vita.cfg";
    char keep[2048];
    size_t keepSize = 0;

    FILE* f = fopen( path, "r" );
    if( f != NULL )
    {
        char line[96];
        bool fits = true;
        while( fits && fgets( line, sizeof( line ), f ) != NULL )
        {
            if( strncmp( line, "fps=", 4 ) == 0 )
            {
                continue;
            }
            const size_t length = strlen( line );
            fits = keepSize + length + 1 < sizeof( keep );
            if( fits )
            {
                memcpy( keep + keepSize, line, length );
                keepSize += length;
                if( keep[ keepSize - 1 ] != '\n' )
                {
                    keep[ keepSize++ ] = '\n';   // last line without a line end
                }
            }
        }
        fclose( f );
        if( !fits )
        {
            return;
        }
    }

    f = fopen( path, "w" );
    if( f != NULL )
    {
        fprintf( f, "fps=%d\n", VitaGetFrameRate() );
        fwrite( keep, 1, keepSize, f );
        fclose( f );
    }
}
#endif

//=============================================================================
// Log output.
//
// The old code did fprintf + fflush (a synchronous write to the memory card,
// 5-8 ms each) plus sceClibPrintf for every line, on the game thread. One
// "Encountered Dynamic Zone" block is 15-30 lines, so it froze the game for
// 100-250 ms: most of the [hitch] ... flow 130-270 ms lines in log.txt were the
// logging itself. Now, after boot, lines go to a ring buffer and a low-priority
// thread writes them to the file every 200 ms. During boot lines are still
// written synchronously, so a hang or crash at startup is not lost.
//=============================================================================
#if defined( RAD_VITA ) && defined( RAD_LOG )
#ifndef RAD_VITA_LOG_CLIBPRINTF
#define RAD_VITA_LOG_CLIBPRINTF 0         // 1 = also echo to sceClibPrintf (slow, only with a debugger attached)
#endif
#define LOG_RING_SIZE   ( 256 * 1024 )
#define LOG_CHUNK_SIZE  ( 32 * 1024 )

static FILE*           s_pLogFile   = NULL;
static SceUID          s_logMutex   = -1;
static SceUID          s_logThread  = -1;
static volatile int    s_logAsync   = 0;     // 0 = synchronous (boot), 1 = ring buffer + thread
static volatile int    s_logQuit    = 0;
static char            s_logRing[LOG_RING_SIZE];
static unsigned        s_logHead    = 0;     // write position (monotonic)
static unsigned        s_logTail    = 0;     // read position (monotonic)
static unsigned        s_logDropped = 0;
static char            s_logChunk[LOG_CHUNK_SIZE];

static unsigned LogDrain( bool flush )
{
    // The mutex only protects head/tail/dropped, and is held for a few instructions. The copy and
    // the file write happen outside it: producers only write into the free part of the ring
    // ([head, tail + SIZE)) and this function only reads [tail, head), so they never overlap.
    // (Before, the 32 KB copy ran under the lock; this thread has the lowest priority, so a
    // preempted drain could leave the game thread blocked in LockMutex for as long as it was starved.)
    sceKernelLockMutex( s_logMutex, 1, NULL );
    const unsigned head = s_logHead;
    const unsigned tail = s_logTail;
    unsigned dropped = s_logDropped;
    s_logDropped = 0;
    sceKernelUnlockMutex( s_logMutex, 1 );

    unsigned avail = head - tail;
    if( avail > LOG_CHUNK_SIZE ) avail = LOG_CHUNK_SIZE;
    unsigned n;
    for( n = 0; n < avail; n++ )
    {
        s_logChunk[n] = s_logRing[ ( tail + n ) % LOG_RING_SIZE ];
    }

    sceKernelLockMutex( s_logMutex, 1, NULL );
    s_logTail = tail + n;
    sceKernelUnlockMutex( s_logMutex, 1 );

    if( s_pLogFile != NULL )
    {
        if( dropped != 0 )
        {
            fprintf( s_pLogFile, "[log] %u bytes dropped (ring buffer full or log busy)\n", dropped );
        }
        if( n != 0 )
        {
            fwrite( s_logChunk, 1, n, s_pLogFile );
        }
        if( flush && ( n != 0 || dropped != 0 ) )
        {
            fflush( s_pLogFile );
        }
    }
    return n;
}

static int LogThreadMain( SceSize args, void* argp )
{
    while( !s_logQuit )
    {
        sceKernelDelayThread( 200 * 1000 );
        while( LogDrain( true ) == LOG_CHUNK_SIZE ) {}
    }
    return 0;
}

static void LogStartAsync()
{
    if( s_pLogFile == NULL || s_logAsync )
    {
        return;
    }
    if( s_logMutex < 0 )
    {
        s_logMutex = sceKernelCreateMutex( "srr2_log", 0, 0, NULL );
    }
#ifndef SCE_KERNEL_LOWEST_PRIORITY_USER
#define SCE_KERNEL_LOWEST_PRIORITY_USER 191
#endif
    if( s_logMutex >= 0 && s_logThread < 0 )
    {
        s_logThread = sceKernelCreateThread( "srr2_log", LogThreadMain, SCE_KERNEL_LOWEST_PRIORITY_USER, 64 * 1024, 0, 0, NULL );
        if( s_logThread >= 0 )
        {
            sceKernelStartThread( s_logThread, 0, NULL );
        }
    }
    if( s_logMutex >= 0 && s_logThread >= 0 )
    {
        s_logAsync = 1;
    }
}

static void LogStopAsync()
{
    if( s_logAsync )
    {
        s_logAsync = 0;
        s_logQuit = 1;
        sceKernelWaitThreadEnd( s_logThread, NULL, NULL );
        s_logThread = -1;
        LogDrain( true );
    }
    if( s_pLogFile != NULL )
    {
        fflush( s_pLogFile );
    }
}
#endif

static void LogOutputFunction( void *userdata, int category, SDL_LogPriority priority, const char *message )
{
#ifdef RAD_VITA
#if !defined( RAD_LOG ) || RAD_VITA_LOG_CLIBPRINTF
    sceClibPrintf( "%s\n", message );
#endif
#ifdef RAD_LOG
    if( s_pLogFile != NULL )
    {
        if( !s_logAsync )
        {
            fprintf( s_pLogFile, "%s\n", message );
            fflush( s_pLogFile );
        }
        else
        {
            const unsigned len = (unsigned)strlen( message );
            // Never block the game / loader thread on the log: if the drain thread holds the
            // mutex right now, drop the line instead of waiting for a lowest-priority thread.
            if( sceKernelTryLockMutex( s_logMutex, 1 ) < 0 )
            {
                __sync_fetch_and_add( &s_logDropped, len + 1 );
            }
            else
            {
                if( ( s_logHead - s_logTail ) + len + 1 <= LOG_RING_SIZE )
                {
                    for( unsigned i = 0; i < len; i++ )
                    {
                        s_logRing[ ( s_logHead + i ) % LOG_RING_SIZE ] = message[i];
                    }
                    s_logRing[ ( s_logHead + len ) % LOG_RING_SIZE ] = '\n';
                    s_logHead += len + 1;
                }
                else
                {
                    s_logDropped += len + 1;
                }
                sceKernelUnlockMutex( s_logMutex, 1 );
            }
        }
    }

#endif
#else
    printf( "%s\n", message );
    fflush( stdout );
#endif
}

//=============================================================================
// Function:    LogProgress
//=============================================================================
//
// Description: Logs a startup milestone (and the free memory on the Vita) so
//              that the point where the game hangs or crashes can be found in
//              the log. Does nothing unless built with SRR2_LOG.
//
//              Besides the kernel free memory it prints the newlib malloc
//              state (mallinfo) and the free/largest-block numbers of the
//              radMemorySpace heaps (User, Cdram, Phycont), because the kernel
//              number alone does not show how full the 192 MB newlib heap is.
//
//              Not static: also called from Win32Platform::InitializeFoundation.
//
//=============================================================================
#ifdef RAD_LOG
#ifdef RAD_VITA
extern IRadMemoryHeap* g_pIRadMemoryHeap_User;
extern IRadMemoryHeap* g_pIRadMemoryHeap_Cdram;
#ifdef RAD_GXM
extern IRadMemoryHeap* g_pIRadMemoryHeap_Phycont;
#endif

static void LogHeap( const char* name, IRadMemoryHeap* heap )
{
    if( heap == NULL )
    {
        return;
    }
    unsigned int totalFree = 0, largestBlock = 0, numObjects = 0, highWater = 0;
    heap->GetStatus( &totalFree, &largestBlock, &numObjects, &highWater );
    rReleasePrintf( "[mem] heap %s: size=%u KB free=%u KB largest=%u KB objects=%u highwater=%u KB\n",
                    name, heap->GetSize() / 1024, totalFree / 1024, largestBlock / 1024,
                    numObjects, highWater / 1024 );
}
#endif

void LogProgress( const char* stage )
{
    if( !g_radLogEnabled )
    {
        return;     // the memory state below takes time to gather
    }
    rReleasePrintf( "[boot] %s\n", stage );
#ifdef RAD_VITA
    SceKernelFreeMemorySizeInfo info;
    info.size = sizeof( info );
    if( sceKernelGetFreeMemorySize( &info ) >= 0 )
    {
        rReleasePrintf( "[mem] free user=%d KB cdram=%d KB phycont=%d KB\n",
                        info.size_user / 1024, info.size_cdram / 1024, info.size_phycont / 1024 );
    }

    // newlib malloc: arena = bytes obtained from sbrk, uordblks = in use, fordblks = free inside the arena
    struct mallinfo mi = mallinfo();
    rReleasePrintf( "[mem] malloc: arena=%d KB used=%d KB free=%d KB\n",
                    mi.arena / 1024, mi.uordblks / 1024, mi.fordblks / 1024 );

    LogHeap( "user", g_pIRadMemoryHeap_User );
    LogHeap( "cdram", g_pIRadMemoryHeap_Cdram );
#ifdef RAD_GXM
    LogHeap( "phycont", g_pIRadMemoryHeap_Phycont );
#endif
#endif
}

// One-line variant for use during gameplay (after every zone). LogProgress walks the whole
// newlib heap (mallinfo) and prints 6 lines; that is fine at boot and every 900 frames, but not
// after every streamed zone.
void LogProgressBrief( const char* stage )
{
    if( !g_radLogEnabled )
    {
        return;
    }
    rReleasePrintf( "[boot] %s\n", stage );
#ifdef RAD_VITA
    SceKernelFreeMemorySizeInfo info;
    info.size = sizeof( info );
    if( sceKernelGetFreeMemorySize( &info ) >= 0 )
    {
        rReleasePrintf( "[mem] free user=%d KB cdram=%d KB\n", info.size_user / 1024, info.size_cdram / 1024 );
    }
#endif
}
#else
#define LogProgress( stage ) ((void)0)
#define LogProgressBrief( stage ) ((void)0)
#endif

//=============================================================================
// Function:    ZoneCacheHasHeadroom
//=============================================================================
//
// Description: Used by RenderManager's zone cache (deferred zone Dump). On the
//              Vita returns true while there is enough free memory to keep one
//              more far-away zone resident: the newlib heap must stay under
//              RAD_VITA_ZONE_CACHE_MAX_MALLOC_KB, and the user and cdram
//              radMemorySpace heaps must keep a minimum free. On other
//              platforms returns false, so the original behaviour (dump
//              immediately) is unchanged.
//
//=============================================================================
#ifdef RAD_VITA
#ifndef RAD_VITA_ZONE_CACHE_MAX_MALLOC_KB
#define RAD_VITA_ZONE_CACHE_MAX_MALLOC_KB ( 200 * 1024 )
#endif
#ifndef RAD_VITA_ZONE_CACHE_MIN_CDRAM_FREE_KB
#define RAD_VITA_ZONE_CACHE_MIN_CDRAM_FREE_KB ( 24 * 1024 )
#endif
#ifndef RAD_VITA_ZONE_CACHE_MIN_USER_FREE_KB
#define RAD_VITA_ZONE_CACHE_MIN_USER_FREE_KB ( 16 * 1024 )
#endif
extern IRadMemoryHeap* g_pIRadMemoryHeap_User;
extern IRadMemoryHeap* g_pIRadMemoryHeap_Cdram;

static bool HeapHasFreeKB( IRadMemoryHeap* heap, unsigned int minFreeKB )
{
    if( heap == NULL )
    {
        return true;
    }
    unsigned int totalFree = 0, largestBlock = 0, numObjects = 0, highWater = 0;
    heap->GetStatus( &totalFree, &largestBlock, &numObjects, &highWater );
    return ( totalFree / 1024 ) >= minFreeKB;
}

bool ZoneCacheHasHeadroom()
{
    struct mallinfo mi = mallinfo();
    if( (unsigned int)mi.uordblks > (unsigned int)RAD_VITA_ZONE_CACHE_MAX_MALLOC_KB * 1024u )
    {
        return false;
    }
    return HeapHasFreeKB( g_pIRadMemoryHeap_Cdram, RAD_VITA_ZONE_CACHE_MIN_CDRAM_FREE_KB )
        && HeapHasFreeKB( g_pIRadMemoryHeap_User, RAD_VITA_ZONE_CACHE_MIN_USER_FREE_KB );
}
#else
bool ZoneCacheHasHeadroom()
{
    return false;
}
#endif

//=============================================================================
// Function:    WinMain
//=============================================================================
//
// Description: Main Windows entry point.
// 
// Parameters:  win32 parameters
//
// Returns:     win32 return.
//
//=============================================================================
extern "C" int main( int argc, char *argv[] )
{
#ifdef __SWITCH__
#ifdef RAD_DEBUG
    socketInitializeDefault();
    nxlinkStdio();
#endif
    romfsInit();
#endif
#ifdef RAD_VITA
	chdir( "ux0:data/simpsons" );
    LoadVitaConfig();
#ifdef RAD_LOG
    g_vitaProfEnabled = g_radLogEnabled && s_vitaProf;
    if( g_radLogEnabled )
    {
        s_pLogFile = fopen( "ux0:data/simpsons/log.txt", "w" );
    }
    rReleasePrintf( "[boot] newlib heap requested: %d MB\n", RAD_VITA_HEAP_MB );
#endif
    SetVitaClocks();
#endif
    LogProgress( "start" );

    //
    // Pick out and store command line settings.
    //
    CommandLineOptions::InitDefaults();
    ProcessCommandLineArguments( argc, argv );
    ProcessCommandLineArgumentsFromFile();

    //
    // Initialize SDL subsystems
    //
#if SDL_MAJOR_VERSION < 3
    SDL_LogSetOutputFunction( LogOutputFunction, NULL );

    SDL_Init( SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER );
#else
    SDL_SetLogOutputFunction( LogOutputFunction, NULL );

    SDL_Init( SDL_INIT_EVENTS | SDL_INIT_VIDEO | SDL_INIT_GAMEPAD );
#endif
    LogProgress( "SDL initialized" );
#ifdef RAD_LOG
    rReleasePrintf( "[boot] SDL error string: [%s]\n", SDL_GetError() );
#endif

    //
    // Have to get FTech setup first so that we can use all the memory services.
    // The initialize window call will fail if another Simpsons window exists. In
    // this case, we exit.
    //
    if( !Win32Platform::InitializeWindow() )
    {
        LogProgress( "InitializeWindow failed" );
        return 0;
    }
    LogProgress( "window initialized" );
    Win32Platform::InitializeFoundation();
    LogProgress( "foundation initialized" );

    srand (Game::GetRandomSeed ());


    // Now disable the default heap
    //
#ifndef RAD_RELEASE
    tName::SetAllocator (GMA_DEBUG);

    //g_HeapActivityTracker.EnableHeapAllocs (GMA_DEFAULT, false);
    //g_HeapActivityTracker.EnableHeapFrees (GMA_DEFAULT, false);
#endif

    HeapMgr()->PushHeap (GMA_PERSISTENT);

    //
    // Instantiate all the singletons before doing anything else.
    //
    CreateSingletons();
    LogProgress( "singletons created" );

    //
    // Construct the platform object.
    //
    Win32Platform* pPlatform = Win32Platform::CreateInstance();
    rAssert( pPlatform != NULL );
    LogProgress( "platform created" );

    //
    // Create the game object.
    //
    Game* pGame = Game::CreateInstance( pPlatform );
    rAssert( pGame != NULL );


    //
    // Initialize the game.
    //
    LogProgress( "game created, initializing" );
    pGame->Initialize();
    LogProgress( "game initialized" );
#if defined( RAD_VITA ) && defined( RAD_LOG )
    // boot is over: from here on log lines are buffered and written by a background thread
    rReleasePrintf( "[boot] frame rate=%d fps, zone cache max=%d\n",
                    g_gxmFrameRate, g_vitaZoneCacheMaxZones );
    LogStartAsync();
#endif

    HeapMgr()->PopHeap (GMA_PERSISTENT);

    //
    // Run it!  Control will not return from here until the game is stopped.
    //
    pGame->Run();
    LogProgress( "game run finished" );
#if defined( RAD_VITA ) && defined( RAD_LOG )
    LogStopAsync();
#endif

    //
    // Terminate the game (this frees all resources allocated by the game).
    //
    pGame->Terminate();

    //
    // Dump all the singletons.
    //
    DestroySingletons();

    //
    // Destroy the game object.
    //
    Game::DestroyInstance();

    //
    // Shutdown the platform.
    //
    pPlatform->ShutdownPlatform();

    //
    // Destroy the game and platform (do it in this order in case the game's 
    // destructor references the platform.
    //
    Win32Platform::DestroyInstance();

    // As a last thing, shut down the memory.
    Win32Platform::ShutdownMemory();

    // Re-enable the default heap
    //
#ifndef RAD_RELEASE
    tName::SetAllocator (RADMEMORY_ALLOC_DEFAULT);
#endif

    //
    // Shutdown SDL subsystems
    //
    SDL_Quit();

    //
    // Pass any error codes back to the operating system.
    //
    return 0;
}


//=============================================================================
// Function:    ProcessCommandLineArguments
//=============================================================================
//
// Description: Pick out the command line options and store them.
// 
// Parameters:  None.
//
// Returns:     None.
//
//=============================================================================
void ProcessCommandLineArguments(int argc, char* argv[])
{
    rDebugPrintf( "*************************************************************************\n" );
    rDebugPrintf( "Command Line Args:\n" );

    //
    // Pick out all the command line options and store them in GameDB.
    // Also dump them to the output for handy dandy viewing.
    //
    for (int i = 1; i < argc; i++ )
    {
        rDebugPrintf( "arg%d: %s\n", i, argv[i] );

        CommandLineOptions::HandleOption( argv[i] );
    }

    if( !CommandLineOptions::Get( CLO_ART_STATS ) )
    {
        //CommandLineOptions::HandleOption( "noheaps" );
    }

    rDebugPrintf( "*************************************************************************\n" );
}



void ProcessCommandLineArgumentsFromFile()
{
#ifndef FINAL

    //Chuck: looking for additional command line args being passed in from a file
    //its for QA testing etc.

    FILE* pfile = fopen( "command.txt", "r" );

    if (pfile != NULL)
    {
        int ret = fseek( pfile, 0, SEEK_END ); 
        rAssert( ret == 0 );

        int len = ftell( pfile );
        rAssertMsg( len < 256, "Command line file too large to process." );

        rewind( pfile );

        if( len > 0 && len < 256 )
        {
            char commandlinestring[256] = {0};

            fgets( commandlinestring, 256, pfile );
                    
            char* argument = strtok(commandlinestring," ");
            while (argument != NULL)
            {
                CommandLineOptions::HandleOption(argument);
                argument=strtok(NULL," ");
            }
        }

        fclose( pfile );
    }
#endif //FINAL
} //end of Function