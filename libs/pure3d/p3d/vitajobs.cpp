//=============================================================================
// See vitajobs.hpp
//=============================================================================
#include <p3d/vitajobs.hpp>
#include <raddebug.hpp>

#include <psp2/kernel/threadmgr.h>

namespace
{
    enum { QUEUE_SIZE = 32, WORKER_STACK_BYTES = 128 * 1024 };

    vitaJob* s_queue[ QUEUE_SIZE ];
    unsigned s_head = 0, s_count = 0;   // protected by s_mutex
    SceUID s_mutex = -1;
    SceUID s_ready = -1;                // counts queued jobs
    bool s_running = false;

    int WorkerMain( SceSize, void* )
    {
        for( ;; )
        {
            sceKernelWaitSema( s_ready, 1, NULL );

            sceKernelLockMutex( s_mutex, 1, NULL );
            vitaJob* job = s_queue[ s_head ];
            s_head = ( s_head + 1 ) % QUEUE_SIZE;
            s_count--;
            sceKernelUnlockMutex( s_mutex, 1 );

            job->function( job->arg );
            __atomic_store_n( &job->done, 1, __ATOMIC_RELEASE );
        }
        return 0;
    }
}

void vitaJobsStart()
{
    if( s_running )
    {
        return;
    }
    s_mutex = sceKernelCreateMutex( "vitajobs", 0, 0, NULL );
    s_ready = sceKernelCreateSema( "vitajobs", 0, 0, QUEUE_SIZE, NULL );
    if( s_mutex < 0 || s_ready < 0 )
    {
        rReleasePrintf( "[jobs] could not create the queue: 0x%08x 0x%08x\n", s_mutex, s_ready );
        return;
    }

    // The two user cores the game's main thread does not need for itself.
    const int cores[ 2 ] = { SCE_KERNEL_CPU_MASK_USER_1, SCE_KERNEL_CPU_MASK_USER_2 };
    int started = 0;
    for( int i = 0; i < 2; i++ )
    {
        const SceUID thread = sceKernelCreateThread( "vitajobs", WorkerMain, 0x10000100, WORKER_STACK_BYTES, 0, cores[ i ], NULL );
        if( thread >= 0 && sceKernelStartThread( thread, 0, NULL ) >= 0 )
        {
            started++;
        }
    }
    s_running = started > 0;
    rReleasePrintf( "[jobs] %d worker threads\n", started );
}

bool vitaJobSubmit( vitaJob* job, vitaJobFunction function, void* arg )
{
    if( !s_running )
    {
        return false;
    }
    job->function = function;
    job->arg = arg;
    job->done = 0;

    sceKernelLockMutex( s_mutex, 1, NULL );
    const bool room = s_count < QUEUE_SIZE;
    if( room )
    {
        s_queue[ ( s_head + s_count ) % QUEUE_SIZE ] = job;
        s_count++;
    }
    sceKernelUnlockMutex( s_mutex, 1 );

    if( room )
    {
        sceKernelSignalSema( s_ready, 1 );
    }
    return room;
}

bool vitaJobIsDone( const vitaJob* job )
{
    return __atomic_load_n( &job->done, __ATOMIC_ACQUIRE ) != 0;
}
