//=============================================================================
// See vitaprof.h
//=============================================================================
#if defined( RAD_VITA ) && defined( RAD_LOG )

#include <debug/vitaprof.h>
#include <raddebug.hpp>

#include <string.h>
#include <stdlib.h>

#include <psp2/kernel/processmgr.h>
#include <psp2/kernel/threadmgr.h>

int g_vitaProfEnabled = 1;      // read from vita.cfg, code/main/win32main.cpp

namespace
{
    enum { MAX_MARKS = 512, MAX_DEPTH = 32, REPORT_ROWS = 36 };

    struct Mark
    {
        const char* name;
        unsigned self;      // microseconds without the marks inside it
        unsigned total;     // microseconds with them
        unsigned calls;
    };

    struct Open
    {
        Mark* mark;
        unsigned start;
        unsigned inner;     // microseconds spent in the marks inside this one
    };

    Mark s_marks[MAX_MARKS];    // open addressing, keyed by the string's address
    Open s_stack[MAX_DEPTH];
    int s_depth = 0;
    int s_mainThread = -1;

    Mark* FindMark( const char* name )
    {
        // The same text can live at several addresses (one per object file): each gets its own
        // slot and the report adds them up.
        unsigned slot = ( (unsigned)(size_t)name >> 2 ) * 2654435761u >> 23;
        for( int i = 0; i < MAX_MARKS; i++ )
        {
            Mark* m = &s_marks[ ( slot + i ) & ( MAX_MARKS - 1 ) ];
            if( m->name == name )
            {
                return m;
            }
            if( m->name == NULL )
            {
                m->name = name;
                return m;
            }
        }
        return NULL;    // full: this mark is not counted
    }

    void Close( int index, unsigned now )
    {
        Open& o = s_stack[ index ];
        const unsigned elapsed = now - o.start;
        if( o.mark != NULL )
        {
            o.mark->total += elapsed;
            o.mark->self += elapsed > o.inner ? elapsed - o.inner : 0;
        }
        if( index > 0 )
        {
            s_stack[ index - 1 ].inner += elapsed;
        }
    }

    int CompareSelf( const void* a, const void* b )
    {
        const unsigned x = ( (const Mark*)a )->self;
        const unsigned y = ( (const Mark*)b )->self;
        return x < y ? 1 : ( x > y ? -1 : 0 );
    }
}

void vitaProfBegin( const char* name )
{
    if( !g_vitaProfEnabled )
    {
        return;
    }
    const int thread = sceKernelGetThreadId();
    if( s_mainThread == -1 )
    {
        s_mainThread = thread;      // the first mark is in Game::Run
    }
    if( thread != s_mainThread || s_depth >= MAX_DEPTH )
    {
        return;
    }
    Mark* m = FindMark( name );
    if( m != NULL )
    {
        m->calls++;
    }
    Open& o = s_stack[ s_depth++ ];
    o.mark = m;
    o.inner = 0;
    o.start = sceKernelGetProcessTimeLow();
}

void vitaProfEnd( const char* name )
{
    if( !g_vitaProfEnabled || s_depth == 0 || sceKernelGetThreadId() != s_mainThread )
    {
        return;
    }
    const unsigned now = sceKernelGetProcessTimeLow();

    // Marks an early return left open are closed with their parent. An end without a begin
    // (a begin that was dropped) changes nothing.
    int found = s_depth - 1;
    while( found >= 0 && ( s_stack[ found ].mark == NULL || strcmp( s_stack[ found ].mark->name, name ) != 0 ) )
    {
        found--;
    }
    if( found < 0 )
    {
        return;
    }
    for( int i = s_depth - 1; i >= found; i-- )
    {
        Close( i, now );
    }
    s_depth = found;
}

void vitaProfAdd( const char* name, unsigned microseconds )
{
    if( !g_vitaProfEnabled )
    {
        return;
    }
    Mark* m = FindMark( name );
    if( m != NULL )
    {
        m->calls++;
        m->self += microseconds;
        m->total += microseconds;
    }
}

void vitaProfReport( unsigned frames )
{
    if( !g_vitaProfEnabled || frames == 0 )
    {
        return;
    }

    // one row per text
    static Mark rows[ MAX_MARKS ];
    int count = 0;
    unsigned calls = 0;
    for( int i = 0; i < MAX_MARKS; i++ )
    {
        if( s_marks[ i ].name == NULL )
        {
            continue;
        }
        calls += s_marks[ i ].calls;
        int r = 0;
        while( r < count && strcmp( rows[ r ].name, s_marks[ i ].name ) != 0 )
        {
            r++;
        }
        if( r == count )
        {
            rows[ count ] = s_marks[ i ];
            count++;
        }
        else
        {
            rows[ r ].self += s_marks[ i ].self;
            rows[ r ].total += s_marks[ i ].total;
            rows[ r ].calls += s_marks[ i ].calls;
        }
    }
    qsort( rows, count, sizeof( Mark ), CompareSelf );

    rReleasePrintf( "[prof] ms per frame, own time / with inner marks / calls per frame (%u marks per frame in all):\n", calls / frames );
    for( int r = 0; r < count && r < REPORT_ROWS; r++ )
    {
        const unsigned self = rows[ r ].self / frames;
        const unsigned total = rows[ r ].total / frames;
        rReleasePrintf( "[prof] %u.%02u / %u.%02u / %u %s\n",
                        self / 1000, ( self / 10 ) % 100,
                        total / 1000, ( total / 10 ) % 100,
                        rows[ r ].calls / frames, rows[ r ].name );
    }

    for( int i = 0; i < MAX_MARKS; i++ )
    {
        s_marks[ i ].self = s_marks[ i ].total = s_marks[ i ].calls = 0;
    }
}

#endif
