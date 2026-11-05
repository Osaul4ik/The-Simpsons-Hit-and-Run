#ifndef __EntityDSG_H__
#define __EntityDSG_H__

//========================================================================
// Copyright (C) 2002 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   IEntityDSG
//
// Description: The IEntityDSG does STUFF
//
// History:     + Initial Implementation		      -- Devin [2002/05/03]
//
//========================================================================

//=================================================
// System Includes
//=================================================
#include <p3d/drawable.hpp>

#ifndef WORLD_BUILDER
#include <render/Culling/NodeFLL.h>
#else
#include "../Culling/NodeFLL.h"
#endif

#include <radtime.hpp>
#include <debug/profiler.h>

class tShadowSkin;
class tShadowMesh;
class SpatialNode;
//=================================================
// Project Includes
//=================================================

//========================================================================
//
// Synopsis:   The IEntityDSG; Synopsis by Inspection.
//
//========================================================================
//#define DONT_DRAW_EXPENSIVE
//#define BREAK_DOWN_PROFILE

//////////////////////////////////////////////////////////////////////////
//
//////////////////////////////////////////////////////////////////////////
class IEntityDSG : 
    public tDrawable,
    public tDrawable::ShaderCallback
    //,public ISortPriority
{
public:
   IEntityDSG();

   virtual rmt::Vector*       pPosition() = 0;
   virtual const rmt::Vector& rPosition() = 0;

   virtual void SetShadow( tShadowSkin* pShadowSkin ){ rAssert ("SetShadow not implemented"); }
   virtual void SetShadow( tShadowMesh* pShadowMesh ){ rAssert ("SetShadow not implemented"); }
   virtual int CastsShadow(){return 0;}
   virtual void DisplayShadow() { rAssert( "DisplayShadow not implemented" ); }
   virtual void DisplaySimpleShadow( void ) { rAssert( false && "DisplaySimpleShadow not implemented" ); }

   virtual void RenderUpdate();

   virtual void SetShader(tShader* pShader, int i){}
   const tName& GetShaderName(){ return mShaderName; }
   const tUID   GetShaderUID(){ return mShaderName.GetUID(); }

   tShader* Process(tShader* pShaders); 

    virtual void GetPosition( rmt::Vector* ipPosn ){}
    virtual void  SetRank(rmt::Vector& irRefPosn, rmt::Vector& mViewVector);
    float Rank();
    float mRank;

#ifdef RAD_VITA
    // Fading in after being loaded while the player is around. Such an entity is drawn with the
    // translucent ones and WorldScene gives it its alpha.
    static unsigned int msFadeNowMs;    // this frame's time
    static unsigned int msFadeMs;       // how long it takes, 0 = no fading ("zonefade=" in vita.cfg)
    unsigned int mFadeStartMs;
    void StartFade() { mFadeStartMs = msFadeMs != 0 ? ( msFadeNowMs | 1 ) : 0; }
    int FadeAlpha()     // 255 = not fading
    {
        if( mFadeStartMs == 0 )
        {
            return 255;
        }
        // the start is rounded up to an odd number, so it can be just ahead of this frame's time
        const int sinceStart = (int)( msFadeNowMs - mFadeStartMs );
        const unsigned int age = sinceStart > 0 ? (unsigned int)sinceStart : 0;
        if( age >= msFadeMs )
        {
            mFadeStartMs = 0;
            return 255;
        }
        return (int)( age * 255u / msFadeMs );
    }
#endif


   bool  mTranslucent;
#ifdef RAD_VITA
   bool  mDrawAfterGuts = false;     // WorldScene::RenderAfterGuts
   bool  mZoneAsleep = false;        // its zone waits in the zone cache: in memory, not drawn
   unsigned mOcclQueryFrame = 0;     // WorldScene::RenderOpaque: the frame of its running query, 0 = none
   unsigned mOcclLastSeen = 0;       // the last frame it was up for drawing
   unsigned short mOcclQueryIndex = 0;
   unsigned char mOcclHidden = 0;    // how many queries in a row found no visible pixel
#endif
   tName mShaderName;
   static bool msDeletionsSafe;

    SpatialNode* mpSpatialNode;

#ifdef BREAK_DOWN_PROFILE
    static char msMarker;
    unsigned int mMicros;

    void BeginProfile(char* iString)
    {
        iString[0] = IEntityDSG::msMarker; 
        BEGIN_PROFILE(iString)
        mMicros = radTimeGetMicroseconds();
    }
    
    void EndProfile(char* iString)
    {
        mMicros = radTimeGetMicroseconds()-mMicros;
        iString[0] = IEntityDSG::msMarker; 
        END_PROFILE(iString)
    }
#endif

#ifdef DONT_DRAW_EXPENSIVE
    bool IsDrawLong();
#endif

protected:
   virtual ~IEntityDSG();
};

#ifdef DONT_DRAW_EXPENSIVE
    #define IS_DRAW_LONG this->IsDrawLong()
#else
    #define IS_DRAW_LONG false
#endif

#ifdef  BREAK_DOWN_PROFILE
    #define DSG_BEGIN_PROFILE(string) {this->BeginProfile(string);}
    #define DSG_END_PROFILE(string)   {this->EndProfile(string);}
    #define DSG_SET_PROFILE(marker) {IEntityDSG::msMarker=marker;}
#else
    #define DSG_BEGIN_PROFILE(string)
    #define DSG_END_PROFILE(string)
    #define DSG_SET_PROFILE(marker)
#endif
/*
   bool operator<(IEntityDSG* x, IEntityDSG* y)
   {
       if(x->mRank==0.0f)
       {
           return x->mShaderUID-y->mShaderUID;
       }
       else
       {
           return 0.1f*(x->mRank-y->mRank);
       }
   }
*/
#endif
