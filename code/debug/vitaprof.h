//=============================================================================
// Lightweight frame profiler for the Vita release log: the BEGIN_PROFILE/END_PROFILE marks
// already in the game feed it, and the time per mark is written next to the [fps] lines.
// "prof=0" in vita.cfg turns it off.
//=============================================================================
#ifndef VITAPROF_H
#define VITAPROF_H

#ifdef __cplusplus
extern "C" {
#endif

// main thread only; other threads are ignored
void vitaProfBegin( const char* name );
void vitaProfEnd( const char* name );

// time measured by the caller, from the thread that takes turns with the main thread (the loader)
void vitaProfAdd( const char* name, unsigned microseconds );

// writes the marks that took the most time and starts counting again
void vitaProfReport( unsigned frames );

#ifdef __cplusplus
}
#endif

#endif
