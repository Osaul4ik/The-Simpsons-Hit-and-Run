//=============================================================================
// Copyright (c) 2002 Radical Games Ltd.  All rights reserved.
//=============================================================================


//=============================================================================
//
// File:        movieplayer.cpp
//
// Subsystem:	Foundation Technologies - Movie
//
// Description:	This file contains the implementation of the Foundation 
//              Technologies movie player
//
// Date:    	May 16, 2002
//
//=============================================================================

//=============================================================================
// Include Files
//=============================================================================

#include <radoptions.hpp>

#ifndef RAD_MOVIEPLAYER_USE_BINK

#include <raddebug.hpp>
#include <radmath/radmath.hpp>
#include "radmoviefile.hpp"
#include "ffmpegmovieplayer.hpp"
#include "audiodatasource.hpp"

#include <vector>
#include <chrono>

extern "C"
{
    #include <libavcodec/avcodec.h>
    #include <libavformat/avformat.h>
    #include <libswscale/swscale.h>
    #include <libswresample/swresample.h>
}

//=============================================================================
// Constants
//=============================================================================

const char * radMovieDebugChannel2 = "radMovie";
unsigned int const radMovie_NoAudioTrack = 0xFFFFFFFF;

//=============================================================================
// Local Definitions
//=============================================================================

//
// If the video of the movie player starts falling behind the audio,
// the movie player will not render frames until it catches up.  This
// value controls how many milliseconds the video can fall behind before
// the catch-up process begins
//

#define RAD_MOVIE_PLAYER_VIDEO_LAG 10

//
// If the decoder cannot keep up (slow CPU), every frame arrives late and the
// catch-up logic would drop all of them, leaving a black screen. To always show
// something, a late frame is still rendered when nothing has been rendered for
// this many milliseconds (the movie then plays as a slideshow instead).
//

#define RAD_MOVIE_PLAYER_MAX_STALL 100

// Milliseconds elapsed since 'start' (used for the load/decode timing logs)
static unsigned int radMovieMsSince( std::chrono::steady_clock::time_point start )
{
    return (unsigned int)std::chrono::duration_cast< std::chrono::milliseconds >(
        std::chrono::steady_clock::now( ) - start ).count( );
}

// Used while playing: report the error and stop the movie.
#define AV_CHK(x) { \
        int error = (x); \
        if ( error < 0 ) { \
            char str[AV_ERROR_MAX_STRING_SIZE] = ""; \
            av_strerror( error, str, AV_ERROR_MAX_STRING_SIZE ); \
            rDebugPrintf( "%s at %s:%d\n", str, __FILE__, __LINE__ ); \
            SetState( IRadMoviePlayer2::NoData ); \
            return; \
        } \
    }

// Used inside Load(): report the error, release everything that was
// allocated so far and leave the player in the NoData state.
#define AV_LOAD_CHK(x) { \
        int error = (x); \
        if ( error < 0 ) { \
            FailLoad( error, #x ); \
            return; \
        } \
    }

//=============================================================================
// Static Members
//=============================================================================

template<> radMoviePlayer* radLinkedClass< radMoviePlayer >::s_pLinkedClassHead = NULL;
template<> radMoviePlayer* radLinkedClass< radMoviePlayer >::s_pLinkedClassTail = NULL;

//=============================================================================
// Public Member Functions
//=============================================================================

//=============================================================================
// radMoviePlayer::radMoviePlayer
//=============================================================================

radMoviePlayer::radMoviePlayer( void )
    :
    radRefCount( 0 ),
    m_refIRadMovieRenderStrategy( NULL ),
    m_refIRadMovieRenderLoop( NULL ),
    m_refIRadStopwatch( NULL ),
    m_State( IRadMoviePlayer2::NoData ),
    m_VideoFrameState( VideoFrame_Unlocked ),
    m_VideoTrackIndex( 0 ),
    m_AudioTrackIndex( 0 ),
    m_PresentationTime( 0 ),
    m_PresentationDuration( 0 ),
    m_LastRenderTime( 0 ),
    m_FramesDecoded( 0 ),
    m_FramesRendered( 0 ),
    m_FramesDropped( 0 ),
    m_DecodeTimeMs( 0 ),
    m_pFormatCtx( NULL ),
    m_pVideoCtx( NULL ),
    m_pAudioCtx( NULL ),
    m_pSwsCtx( NULL ),
    m_pSwrCtx( NULL ),
    m_pPacket( NULL ),
    m_pVideoFrame( NULL ),
    m_pAudioFrame( NULL ),
    m_AudioSource( 0 )
{
    radTimeCreateStopwatch( &m_refIRadStopwatch, radTimeUnit_Millisecond, GetThisAllocator( ) );
}

//=============================================================================
// radMoviePlayer::~radMoviePlayer
//=============================================================================

radMoviePlayer::~radMoviePlayer( void )
{
}

//=============================================================================
// radMoviePlayer::Initialize
//=============================================================================

void radMoviePlayer::Initialize
( 
    IRadMovieRenderLoop * pIRadMovieRenderLoop, 
    IRadMovieRenderStrategy * pIRadMovieRenderStrategy
)
{
    rAssert( pIRadMovieRenderStrategy != NULL );

    // Hang on to the loop and strategy

    m_refIRadMovieRenderLoop = pIRadMovieRenderLoop;
    m_refIRadMovieRenderStrategy = pIRadMovieRenderStrategy;

    //
    // Update the state
    //

    SetState( IRadMoviePlayer2::NoData );
}

//=============================================================================
// radMoviePlayer::Render
//=============================================================================

bool radMoviePlayer::Render( void )
{
    rAssert( m_refIRadMovieRenderStrategy != NULL );
    rAssert( m_pVideoFrame != NULL );
        
    if( m_pVideoFrame != NULL )
    {
        bool ret = m_refIRadMovieRenderStrategy->Render( );

        m_LastRenderTime = m_refIRadStopwatch->GetElapsedTime( );
        ++m_FramesRendered;

        ALint state;
        alGetSourcei( m_AudioSource, AL_SOURCE_STATE, &state );
        if( state != AL_PLAYING )  // have we fallen behind, if so reset
            alSourcePlay( m_AudioSource );

        FlushAudioQueue();

        //
        // The video frame has been used.  The next can be decoded
        //

        m_VideoFrameState = VideoFrame_Unlocked;

        return ret;
    }
    else
    {
        rAssert( false );
        return false;
    }
}

//=============================================================================
// radMoviePlayer::Load
//=============================================================================

void radMoviePlayer::Load( const char * pVideoFileName, unsigned int audioTrackIndex )
{   
    rAssert( m_State == IRadMoviePlayer2::NoData );
    rAssert( pVideoFileName != NULL );

    // rReleasePrintf, not rDebugPrintf: the latter is compiled out in release builds
    // and these lines are needed to diagnose movies on the device (log.txt).
    rReleasePrintf( "radMoviePlayer: Loading %s\n", pVideoFileName );

    const std::chrono::steady_clock::time_point loadStart = std::chrono::steady_clock::now( );

    m_LastRenderTime = 0;
    m_FramesDecoded = 0;
    m_FramesRendered = 0;
    m_FramesDropped = 0;
    m_DecodeTimeMs = 0;

    m_refIRadStopwatch->Stop( );
    m_refIRadStopwatch->Reset( );

    //
    // Reset the variables
    //

    m_VideoFrameState = VideoFrame_Unlocked;

    SetState( IRadMoviePlayer2::Loading );

    m_pFormatCtx = avformat_alloc_context();
    AV_LOAD_CHK( avformat_open_input( &m_pFormatCtx, pVideoFileName, NULL, NULL ) );
    const unsigned int openMs = radMovieMsSince( loadStart );
    AV_LOAD_CHK( avformat_find_stream_info( m_pFormatCtx, NULL ) );
    const unsigned int probeMs = radMovieMsSince( loadStart ) - openMs;

    const AVCodec* pVideoCodec = NULL;
    int videoIndex = av_find_best_stream( m_pFormatCtx, AVMEDIA_TYPE_VIDEO, -1, -1, &pVideoCodec, 0 );
    if( videoIndex < 0 )
    {
        // Most likely AVERROR_DECODER_NOT_FOUND: the FFmpeg build has no
        // decoder for the video stream (e.g. Theora in the .rmv files).
        // Say which codec is missing, then bail out instead of indexing
        // streams[] with a negative error code.
        for( unsigned int i = 0; i < m_pFormatCtx->nb_streams; ++i )
        {
            const AVCodecParameters* p = m_pFormatCtx->streams[i]->codecpar;
            if( p->codec_type == AVMEDIA_TYPE_VIDEO )
            {
                rReleasePrintf( "radMoviePlayer: video stream %u uses codec '%s' (id %d)\n",
                    i, avcodec_get_name( p->codec_id ), (int)p->codec_id );
            }
        }
        FailLoad( videoIndex, "av_find_best_stream( video )" );
        return;
    }
    m_VideoTrackIndex = videoIndex;
    AVCodecParameters* pVideoParams = m_pFormatCtx->streams[m_VideoTrackIndex]->codecpar;
    m_pVideoCtx = avcodec_alloc_context3( pVideoCodec );
    if( m_pVideoCtx == NULL )
    {
        FailLoad( AVERROR( ENOMEM ), "avcodec_alloc_context3( video )" );
        return;
    }
    AV_LOAD_CHK( avcodec_parameters_to_context( m_pVideoCtx, pVideoParams ) );
    AV_LOAD_CHK( avcodec_open2( m_pVideoCtx, pVideoCodec, NULL ) );

#ifndef RAD_VITAGL
    m_pSwsCtx = sws_getContext(
        pVideoParams->width,
        pVideoParams->height,
        AV_PIX_FMT_YUV420P,
        pVideoParams->width,
        pVideoParams->height,
        AV_PIX_FMT_BGRA,
        0, NULL, NULL, NULL
    );
#endif

    if( audioTrackIndex != radMovie_NoAudioTrack )
    {
        const AVCodec* pAudioCodec = NULL;
        int audioIndex = av_find_best_stream( m_pFormatCtx, AVMEDIA_TYPE_AUDIO, audioTrackIndex + 1, -1, &pAudioCodec, 0 );
        if( audioIndex < 0 )
        {
            // No usable audio track (missing or no decoder): play the video silently.
            rReleasePrintf( "radMoviePlayer: no usable audio track %u (error %d), playing without sound\n",
                audioTrackIndex, audioIndex );
            m_AudioTrackIndex = 0;
        }
        else
        {
            m_AudioTrackIndex = audioIndex;
            AVCodecParameters* pAudioParams = m_pFormatCtx->streams[m_AudioTrackIndex]->codecpar;
            m_pAudioCtx = avcodec_alloc_context3( pAudioCodec );
            if( m_pAudioCtx == NULL )
            {
                FailLoad( AVERROR( ENOMEM ), "avcodec_alloc_context3( audio )" );
                return;
            }
            AV_LOAD_CHK( avcodec_parameters_to_context( m_pAudioCtx, pAudioParams ) );
            AV_LOAD_CHK( avcodec_open2( m_pAudioCtx, pAudioCodec, NULL ) );

            AVChannelLayout layout = { AV_CHANNEL_ORDER_NATIVE, 2, AV_CH_LAYOUT_STEREO };
            AV_LOAD_CHK( swr_alloc_set_opts2( &m_pSwrCtx,
                &layout,
                AV_SAMPLE_FMT_S16,
                pAudioParams->sample_rate,
                &pAudioParams->ch_layout,
                (AVSampleFormat)pAudioParams->format,
                pAudioParams->sample_rate,
                0,
                NULL ) );
            AV_LOAD_CHK( swr_init( m_pSwrCtx ) );
        }
    }
    else
    {
        m_AudioTrackIndex = 0;
    }

    m_pPacket = av_packet_alloc();
    m_pVideoFrame = av_frame_alloc();
    m_pAudioFrame = av_frame_alloc();

    alGenSources( 1, &m_AudioSource );
    alSourcei( m_AudioSource, AL_SOURCE_RELATIVE, AL_TRUE );

    //
    // Initialize the render strategy & and pass the file to
    // the decoder to deal with
    //

    m_refIRadMovieRenderStrategy->ChangeParameters( pVideoParams->width, pVideoParams->height );

    //
    // Print out some helpful information
    //
    const AVStream* pVideoStream = m_pFormatCtx->streams[m_VideoTrackIndex];
    rReleasePrintf( "radMoviePlayer: Summary %dx%d codec=%s pix_fmt=%s fps=%d/%d (video stream %d, audio track %d) "
                    "open=%ums probe=%ums\n",
        pVideoParams->width, pVideoParams->height,
        avcodec_get_name( pVideoParams->codec_id ),
        av_get_pix_fmt_name( (AVPixelFormat)pVideoParams->format ),
        pVideoStream->avg_frame_rate.num, pVideoStream->avg_frame_rate.den,
        m_VideoTrackIndex, m_AudioTrackIndex,
        openMs, probeMs );

    // The buffered data source's input must be set before the stream player's

    SetState( IRadMoviePlayer2::ReadyToPlay );

    Service( );
}

//=============================================================================
// radMoviePlayer::FailLoad
//
// Called when Load() cannot set the movie up. Logs the error, frees whatever
// was allocated so far and leaves the player in the NoData state, which the
// FMV player treats as "nothing to play" (the movie is simply skipped).
//=============================================================================

void radMoviePlayer::FailLoad( int error, const char * what )
{
    char str[AV_ERROR_MAX_STRING_SIZE] = "";
    av_strerror( error, str, AV_ERROR_MAX_STRING_SIZE );
    rReleasePrintf( "radMoviePlayer: Load failed in %s: %s (%d)\n", what, str, error );

#ifndef RAD_VITAGL
    sws_freeContext( m_pSwsCtx );
    m_pSwsCtx = NULL;
#endif
    swr_free( &m_pSwrCtx );
    av_packet_free( &m_pPacket );
    av_frame_free( &m_pVideoFrame );
    av_frame_free( &m_pAudioFrame );
    avcodec_free_context( &m_pVideoCtx );
    avcodec_free_context( &m_pAudioCtx );
    avformat_close_input( &m_pFormatCtx );   // also frees the context if open_input failed

    m_refIRadStopwatch->Stop( );
    m_refIRadStopwatch->Reset( );

    m_VideoFrameState = VideoFrame_Unlocked;
    SetState( IRadMoviePlayer2::NoData );
}

//=============================================================================
// radMoviePlayer::LogStats
//
// One line per movie telling how playback went. If 'decode_time' is close to or
// larger than 'played', the CPU cannot decode the movie in real time.
//=============================================================================

void radMoviePlayer::LogStats( const char * reason )
{
    rReleasePrintf( "radMoviePlayer: %s: decoded=%u rendered=%u dropped=%u decode_time=%ums played=%ums last_pts=%ums\n",
        reason, m_FramesDecoded, m_FramesRendered, m_FramesDropped, m_DecodeTimeMs,
        m_refIRadStopwatch->GetElapsedTime( ), m_PresentationTime );
}

//=============================================================================
// radMoviePlayer::Unload
//=============================================================================

void radMoviePlayer::Unload( void )
{
    if( m_State != IRadMoviePlayer2::NoData )
    {
        alSourceStop( m_AudioSource );
        FlushAudioQueue();
        alDeleteSources( 1, &m_AudioSource );

#ifndef RAD_VITAGL
        sws_freeContext( m_pSwsCtx );
#endif
        swr_free( &m_pSwrCtx );
        av_packet_free( &m_pPacket );
        av_frame_free( &m_pVideoFrame );
        av_frame_free( &m_pAudioFrame );
        avcodec_free_context( &m_pVideoCtx );
        avcodec_free_context( &m_pAudioCtx );
        avformat_close_input( &m_pFormatCtx );
        avformat_free_context( m_pFormatCtx );

        m_refIRadStopwatch->Stop( );
        m_refIRadStopwatch->Reset( );

        m_VideoFrameState = VideoFrame_Unlocked;

        SetState( IRadMoviePlayer2::NoData );
    }
}

//=============================================================================
// radMoviePlayer::Play
//=============================================================================

void radMoviePlayer::Play( void )
{
    if( m_State == IRadMoviePlayer2::ReadyToPlay )
    {
        SetState( IRadMoviePlayer2::Playing );
        InternalPlay( );
    }
    else if( m_State == IRadMoviePlayer2::Loading )
    {
        SetState( IRadMoviePlayer2::LoadToPlay );
    }
}

//=============================================================================
// radMoviePlayer::Pause
//=============================================================================

void radMoviePlayer::Pause( void )
{
    if( m_State == IRadMoviePlayer2::Playing )
    {
        m_refIRadStopwatch->Stop( );
        SetState( IRadMoviePlayer2::ReadyToPlay );
    }
    else if( m_State == IRadMoviePlayer2::LoadToPlay )
    {
        SetState( Loading );
    }
}

//=============================================================================
// radMoviePlayer::SetPan
//=============================================================================

void radMoviePlayer::SetPan( float pan )
{
    // Not supported
}

//=============================================================================
// radMoviePlayer::GetPan
//=============================================================================

float radMoviePlayer::GetPan( void )
{
    return 0.0f;
}

//=============================================================================
// radMoviePlayer::SetVolume
//=============================================================================

void radMoviePlayer::SetVolume( float volume )
{
    // Not supported
}

//=============================================================================
// radMoviePlayer::GetVolume
//=============================================================================

float radMoviePlayer::GetVolume( void )
{
    return 1.0f;
}

//=============================================================================
// radMoviePlayer::GetState
//=============================================================================

IRadMoviePlayer2::State radMoviePlayer::GetState( void )
{
    return m_State;
}

//=============================================================================
// radMoviePlayer::GetVideoFrameInfo
//=============================================================================

bool radMoviePlayer::GetVideoFrameInfo( VideoFrameInfo * frameInfo)
{
    rAssert( frameInfo != NULL );

    if( m_State == IRadMoviePlayer2::Playing ||
        m_State == IRadMoviePlayer2::ReadyToPlay )
    {
        ( * frameInfo ).Width = m_pVideoFrame->width;
        ( * frameInfo ).Height = m_pVideoFrame->height;
        return true;
    }
    else
    {
        return false;
    }
}

//=============================================================================
// radMoviePlayer::GetFrameRate
//=============================================================================

float radMoviePlayer::GetFrameRate( void )
{
    // Sweet little lies, frame number is in milliseconds
    return 1000.0f;
}

//=============================================================================
// radMoviePlayer::GetCurrentFrameNumber
//=============================================================================

unsigned int radMoviePlayer::GetCurrentFrameNumber( void )
{
    return m_PresentationTime;
}

//=============================================================================
// radMoviePlayer::Service
//=============================================================================

void radMoviePlayer::Service( void )
{
    //
    // Start off by letting the decoder figure out where it is
    //

    //
    // Now figure out where we are.
    //



    if( m_State == IRadMoviePlayer2::NoData )
    {
        // Nothing to do
    }
    else if( m_State == IRadMoviePlayer2::Playing )
    {
        //
        // If the video frame is unlocked, we can issue the next decode request.
        //

        if( m_VideoFrameState == VideoFrame_Unlocked )
        {
            //
            // Keep an eye on the states of things to detect the end of the movie
            //

            if( av_read_frame( m_pFormatCtx, m_pPacket ) >= 0 )
            {
                if( m_pPacket->stream_index == m_VideoTrackIndex )
                {
                    const std::chrono::steady_clock::time_point decodeStart = std::chrono::steady_clock::now( );

                    AV_CHK( avcodec_send_packet( m_pVideoCtx, m_pPacket ) );

                    //
                    // Decompress all pending video frames
                    //

                    while( avcodec_receive_frame( m_pVideoCtx, m_pVideoFrame ) >= 0 )
                    {
                        ++m_FramesDecoded;

                        // Reset the list of render destinations

                        m_refIRadMovieRenderStrategy->ResetDestinations();

                        // Now go through the list destinations until we've filled 
                        // them all up with the freshly decoded data

                        IRadMovieRenderStrategy::LockedDestination dest;

                        while( m_refIRadMovieRenderStrategy->LockNextDestination( &dest ) > 0 )
                        {
                            uint8_t* pDest = (uint8_t*)dest.m_pDest;

#ifdef RAD_VITAGL
                            // The destination is a tightly packed YUV420P image (stride == width).
                            // FFmpeg's planes are padded (linesize >= width), so copy row by row
                            // instead of copying linesize * height bytes in one go.
                            {
                                const int w = m_pVideoFrame->width;
                                const int h = m_pVideoFrame->height;
                                const int cw = ( w + 1 ) / 2;
                                const int ch = ( h + 1 ) / 2;

                                for( int y = 0; y < h; ++y )
                                {
                                    memcpy( pDest, m_pVideoFrame->data[0] + y * m_pVideoFrame->linesize[0], w );
                                    pDest += w;
                                }
                                for( int p = 1; p <= 2; ++p )
                                {
                                    for( int y = 0; y < ch; ++y )
                                    {
                                        memcpy( pDest, m_pVideoFrame->data[p] + y * m_pVideoFrame->linesize[p], cw );
                                        pDest += cw;
                                    }
                                }
                            }
#else

                            // If one of these copies fail, we'll have to skip this frame
                            // and not iterate the loop
                            if( sws_scale( m_pSwsCtx,
                                m_pVideoFrame->data, m_pVideoFrame->linesize,
                                dest.m_SrcPosY, m_pVideoFrame->height - dest.m_SrcPosY,
                                &pDest, &dest.m_DestPitch ) >= 0 )
#endif
                            {
                                AVRational rational = m_pFormatCtx->streams[0]->time_base;
                                m_PresentationTime = (m_pVideoFrame->pts * rational.num * 1000) / rational.den;
                                m_PresentationDuration = (m_pVideoFrame->duration * rational.num * 1000) / rational.den;
                                m_VideoFrameState = VideoFrame_Locked;
                            }

                            m_refIRadMovieRenderStrategy->UnlockDestination();
                        }
                    }
                }
                else if( m_AudioTrackIndex > 0 && m_pPacket->stream_index == m_AudioTrackIndex )
                {
                    AV_CHK( avcodec_send_packet( m_pAudioCtx, m_pPacket ) );

                    //
                    // Decompress all pending audio frames
                    //

                    while( avcodec_receive_frame( m_pAudioCtx, m_pAudioFrame ) >= 0 )
                    {
                        uint8_t* output;
                        int outSamples = swr_get_out_samples( m_pSwrCtx, m_pAudioFrame->nb_samples );
                        av_samples_alloc( &output, NULL, 2, outSamples,
                                     AV_SAMPLE_FMT_S16, 0 );
                        outSamples = swr_convert( m_pSwrCtx, &output, outSamples,
                                                  (const uint8_t**)m_pAudioFrame->data, m_pAudioFrame->nb_samples );
                        int bufferSize = av_samples_get_buffer_size( NULL, 2,
                                outSamples, AV_SAMPLE_FMT_S16, 0 );

                        // TODO: Add buffer queuing support to radsound and move this code to that module.
                        ALuint buffer;
                        alGenBuffers( 1, &buffer );
                        alBufferData( buffer, AL_FORMAT_STEREO16, output, bufferSize, m_pAudioFrame->sample_rate );
                        alSourceQueueBuffers( m_AudioSource, 1, &buffer );
                        av_freep( &output );
                    }
                }

                av_packet_unref( m_pPacket );
            }
            else
            {
                rDebugChannelPrintf( radMovieDebugChannel2, "radMoviePlayer: Out of data at [%lld]\n", m_pVideoFrame->pts );

                // We've hit the end of the movie.  Unload!

                Unload( );
                return;
            }
        }

        if( m_VideoFrameState == VideoFrame_Locked )
        {
            //
            // We will render the frame at the appropriate time.  We know that the 
            // image will be presented on a vsync.  As long as the time between now
            // and the presentation time is less than a vsync period, we can begin
            // rendering
            //

            if( m_refIRadMovieRenderLoop != NULL )
            {
                // If the client gave us a render loop, initiate a render of the new frame.
                // It's okay for us to fall a little behind the presentation time.  We might
                // catch up without dropping frames.  If we do fall far enough behind, throw
                // away frames until we're all the way caught up

                unsigned int currentTime = m_refIRadStopwatch->GetElapsedTime();
                
                if( currentTime > m_PresentationTime &&
                    currentTime <= m_PresentationTime + m_PresentationDuration + RAD_MOVIE_PLAYER_VIDEO_LAG )
                {
                    m_refIRadMovieRenderLoop->IterateLoop( this );
                }
                else if( currentTime > m_PresentationTime + m_PresentationDuration )
                {
                    rTunePrintf( "radMoviePlayer: NOT RENDERING THIS FRAME (must catch up)\n" );
                    rTunePrintf( "current time[ %d ] expected presentation time[ %d ~ %d ]\n",
                        currentTime, m_PresentationTime, m_PresentationTime + m_PresentationDuration );

                    ALint state;
                    alGetSourcei( m_AudioSource, AL_SOURCE_STATE, &state );
                    if( state != AL_STOPPED )  // allow audio to re-sync
                        alSourceStop( m_AudioSource );

                    //
                    // Not rendering will save us up to a vsync period this round.
                    // Eventually we're bound to catch up
                    //

                    m_VideoFrameState = VideoFrame_Unlocked;
                }
            }
        }
    }
}

//=============================================================================
// radMoviePlayer::SetState
//=============================================================================

void radMoviePlayer::SetState( IRadMoviePlayer2::State state )
{
    if( state != m_State )
    {
        rDebugChannelPrintf( radMovieDebugChannel2, "radMoviePlayer::SetState [%d]\n", state );
        m_State = state;
    }
}

//=============================================================================
// radMoviePlayer::InternalPlay
//=============================================================================

void radMoviePlayer::InternalPlay( void )
{
    m_refIRadStopwatch->Start( );
    Service( );
}

//=============================================================================
// radMoviePlayer::FlushAudioQueue
//=============================================================================

void radMoviePlayer::FlushAudioQueue( void )
{
    // Flush the queue, deleting processed buffers
    ALint processed = 0;
    alGetSourcei( m_AudioSource, AL_BUFFERS_PROCESSED, &processed );
    if( processed > 0 )
    {
        std::vector<ALuint> buffers( processed );
        alSourceUnqueueBuffers( m_AudioSource, processed, buffers.data() );
        alDeleteBuffers( processed, buffers.data() );
    }
}

//=============================================================================
// Function:    radMoviePlayerCreate2
//=============================================================================

IRadMoviePlayer2 * radMoviePlayerCreate2( radMemoryAllocator alloc )
{
	return new( alloc )radMoviePlayer( );
}

//=============================================================================
// Function:    radMovieInitialize2
//=============================================================================

void radMovieInitialize2( radMemoryAllocator alloc )
{
}

//=============================================================================
// Function:    radMovieTerminate2
//=============================================================================

void radMovieTerminate2( void )
{
}

//=============================================================================
// Function:    radMovieService2
//=============================================================================

void radMovieService2( void )
{
    ref< radMoviePlayer > refRadMoviePlayer = radMoviePlayer::GetLinkedClassHead( );

    while( refRadMoviePlayer != NULL )
    {
        refRadMoviePlayer->Service( );

        refRadMoviePlayer = refRadMoviePlayer->GetLinkedClassNext( );
    }
}

#endif // ! RAD_MOVIEPLAYER_USE_BINK