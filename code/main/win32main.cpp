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
#define RAD_VITA_HEAP_MB 192
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

//========================================
// Forward Declarations
//========================================
static void ProcessCommandLineArguments( int argc, char *argv[] );

static void ProcessCommandLineArgumentsFromFile();

#if defined( RAD_VITA ) && defined( RAD_LOG )
static FILE* s_pLogFile = NULL;
#endif

static void LogOutputFunction( void *userdata, int category, SDL_LogPriority priority, const char *message )
{
#ifdef RAD_VITA
    sceClibPrintf( "%s\n", message );
#ifdef RAD_LOG
    if( s_pLogFile != NULL )
    {
        fprintf( s_pLogFile, "%s\n", message );
        fflush( s_pLogFile );
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
#else
#define LogProgress( stage ) ((void)0)
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
#ifdef RAD_LOG
    s_pLogFile = fopen( "ux0:data/simpsons/log.txt", "w" );
    rReleasePrintf( "[boot] newlib heap requested: %d MB\n", RAD_VITA_HEAP_MB );
#endif
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

    HeapMgr()->PopHeap (GMA_PERSISTENT);

    //
    // Run it!  Control will not return from here until the game is stopped.
    //
    pGame->Run();
    LogProgress( "game run finished" );

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