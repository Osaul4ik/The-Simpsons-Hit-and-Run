//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


#include <p3d/buildconfig.hpp>

#ifndef _FILEFTT_HPP_
#define _FILEFTT_HPP_

#include <p3d/file.hpp>
#include <raddebug.hpp>
#include <radfile.hpp>

class tFileFTT : public tFile, public IRadFileCompletionCallback
{
public:
    tFileFTT(const char* filename, bool synchronous = true);

    bool     EndOfFile();
    unsigned GetSize();
    unsigned GetPosition();
    void     Advance(unsigned offset);
    void     SetPosition(int offset);

    bool   GetData(void* buf, unsigned nbytes, DataType);

    void   SetCompressed(bool b);
    void   SetUncompressedSize(int size);

    void   OnFileOperationsComplete(void*);
    void   WaitForCompletion( void );

    void   AddRef() { tFile::AddRef(); }         
    void   Release() { tFile::Release(); }

    static void SetYieldTime(unsigned ms);

    // Vita only (no-op elsewhere). 'true' on loading screens: the loader thread gets long
    // slices and sleeps briefly while waiting for I/O instead of handing a whole frame back
    // to the main thread. 'false' in gameplay: short slices, so streaming does not hurt the
    // frame rate.
    static void SetLoadingMode(bool loading);

    // The loader takes turns with the main thread. Between its turns the game runs.
    static void SetLoaderThread(void);          // called by the loader
    static bool IsLoaderThread(void);
    static void YieldIfDueOnLoader(void);       // for long work that does not read the file
    static bool IsOnLoadingScreen();
    static unsigned GameplaySliceUs(int frameLeftUs, unsigned zoneLoadingMs, bool urgent);
    static void SetGameplaySlice(unsigned microseconds);
    static void WaitForJob(const struct vitaJob* job);  // loader only: the game runs meanwhile

    // Vita only (no-op elsewhere). Called by the loader thread between top-level chunks:
    // hands control back to the main thread if the loader's slice has expired. Chunk handlers
    // only yielded while reading data, so a chunk that is mostly processing (texture and
    // vertex buffer creation, spatial trees) used to keep the main thread waiting.
    static void YieldIfDue(void);

protected:

    IRadFile*    m_pIRadFile;
    int fileSize;
    int compressedFileSize;

    int position;
    int filePosition;

    int currentCache;

    char* cache[2];
    char* uncompressedcache;

    char* currentBuffer;
    int currentPos;
    int currentSize;
    int cycle_size;

    char* compressedBuffer;
    int compressedPos;
    int compressedSize;
    
    int nextSize;

    int bufferSize;
    
    bool done; 

    static unsigned mTaskedOutTime;

    void OpenFile( void );
    void ReadBuffer(char* buf);
    void FillBuffer();
    void AdvanceCycle(int numBytes);
    void Decompress (const unsigned char* input, unsigned int inputsize,
                            unsigned char* output, unsigned int outputsize);

    virtual ~tFileFTT();

};

#endif