//=============================================================================
// Worker threads on the Vita's other two user cores.
//
// A job may only touch memory given to it: no game heaps (radMemory, new, MallocTemp), no Pure3D
// objects, no GXM context. The main and loader threads take turns and rely on that; workers
// really run at the same time. The caller keeps the job and its data alive until it is done.
//=============================================================================
#ifndef VITAJOBS_HPP
#define VITAJOBS_HPP

typedef void (*vitaJobFunction)(void* arg);

struct vitaJob
{
    vitaJobFunction function;
    void* arg;
    volatile int done;
};

// Starts the workers. Without this (or if it fails) vitaJobSubmit returns false.
void vitaJobsStart();

// Queues a job. False if it was not queued (workers not running, queue full): the caller does the
// work itself.
bool vitaJobSubmit(vitaJob* job, vitaJobFunction function, void* arg);

// Pairs with the worker's store, so everything the job wrote is visible after this returns true.
bool vitaJobIsDone(const vitaJob* job);

#endif
