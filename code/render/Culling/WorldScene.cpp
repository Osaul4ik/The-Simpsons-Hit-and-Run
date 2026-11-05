#ifdef RAD_VITA
#include <pddi/gxm/gxm.hpp>
#include <pddi/gxm/context.hpp>
#endif
#include <typeinfo>
#include <render/Culling/WorldScene.h>
#include <render/Culling/SpatialTreeFactory.h>
#include <render/Culling/SpatialTree.h>
#include <render/Culling/SpatialTreeIter.h>
#include <render/DSG/IntersectDSG.h>
#include <render/DSG/StaticPhysDSG.h>
#include <render/DSG/StaticEntityDSG.h>
#include <render/DSG/DynaPhysDSG.h>
#include <render/DSG/FenceEntityDSG.h>
#include <render/DSG/animcollisionentitydsg.h>
#include <render/DSG/animentitydsg.h>
#include <roads/roadsegment.h>
#include <pedpaths/pathsegment.h>
#include <meta/triggervolume.h>
#include <algorithm>
#include <functional>

#include <render/Culling/NodeFLL.h>

#include <render/Culling/Matrix3f.h>
#include <camera/supercammanager.h>

#include <raddebugwatch.hpp>
#include <radtime.hpp>

#include <worldsim/worldphysicsmanager.h>

#include <worldsim/avatarmanager.h>
#include <worldsim/avatar.h>
#include <worldsim/redbrick/vehicle.h>
#include <worldsim/character/character.h>
#include <pddi/pdditype.hpp>

#ifdef DEBUGWATCH
#include <simcollision/collisiondisplay.hpp>
#include <simcollision/collisionmanager.hpp>
#include <simcommon/simutility.hpp>
#endif

#ifdef RAD_DEBUG
#include <mission/missionmanager.h>
#include <mission/objectives/missionobjective.h>
#include <meta/triggervolumetracker.h>
#include <gameflow/gameflow.h>
#include <contexts/contextenum.h>
#endif

#include <debug/profiler.h>

#include <stdlib.h>

#ifdef RAD_GAMECUBE
#include <pddi/gamecube/gcrefractionshader.hpp>
#endif

#define TEST_DISTRIBUTED_SORT
#define ZSORT_RENDER
//#define TRACK_SHADERS
//#define TEST_WHOLE_TREE

//For debug section
//#include <render/Culling/../debuginfo.hpp"
//#include <render/Culling/../../profiler/profiler.hpp"
//For test debugging (camera)
//#include <render/Culling/../../main/globals.hpp"
//#include <render/Culling/../../main/gamesettings.hpp"
//#include <render/Culling/../../worldsim/supercam.hpp"
//#include <render/Culling/../../worldsim/player.hpp"

//static Vector3f TODO_GRANULARITY(160.0f, 2000.0f, 160.0f);
//static Vector3f TODO_GRANULARITY(240.0f, 2000.0f, 240.0f);
//static Vector3f TODO_GRANULARITY(10.0f, 2000.0f, 10.0f);
//static Vector3f TODO_GRANULARITY(200.0f, 2000.0f, 200.0f);
#ifdef RAD_GAMECUBE
static Vector3f TODO_GRANULARITY(40.0f, 2000.0f, 40.0f);
#else
static Vector3f TODO_GRANULARITY(20.0f, 2000.0f, 20.0f);
#endif
//static Vector3f TODO_GRANULARITY(120.0f, 2000.0f, 120.0f);

//defines
//#define RENDER_W_DLIST
#ifdef ZSORT_RENDER
// Replace all the following qsort and stl compare functions with stl function objects

inline bool gZSortCompare( IEntityDSG* pArg1, IEntityDSG* pArg2 )
{
    return pArg1->mRank < pArg2->mRank;
}

inline bool gShaderCompare( const WorldScene::zSortBlah& pArg1, const WorldScene::zSortBlah& pArg2 )
{
    return pArg1.shaderUID < pArg2.shaderUID;        
}

#ifdef RAD_VITA
// The list is drawn from its end: the far band goes first in the list, so the near one is drawn first.
inline bool gBandShaderCompare( const WorldScene::zSortBlah& pArg1, const WorldScene::zSortBlah& pArg2 )
{
    if( pArg1.distanceBand != pArg2.distanceBand )
    {
        return pArg1.distanceBand > pArg2.distanceBand;
    }
    return pArg1.shaderUID < pArg2.shaderUID;
}
#endif

inline bool gTestZ( IEntityDSG* pArg1, IEntityDSG* pArg2 )
{
    return pArg1->mRank < pArg2->mRank;
}
  
inline bool gTestShader( IEntityDSG* pArg1, IEntityDSG* pArg2 )
{
    return pArg1->GetShaderUID() < pArg2->GetShaderUID();
}

/*int gZSortCompare( const void *arg1, const void *arg2 )
{
	return (int)(0.1f*((*(IEntityDSG**)arg1)->Rank() - (*(IEntityDSG**)arg2)->Rank())); 
}

int gShaderCompare( const void *arg1, const void *arg2 )
{
    return (int)(((WorldScene::zSortBlah*)arg1)->shaderUID - ((WorldScene::zSortBlah*)arg2)->shaderUID); 
}

bool gTestZ( IEntityDSG* arg1, IEntityDSG* arg2 )
{
    if( arg1->mRank < arg2->mRank )
        return true;
    else
        return false;
//	return (int)(0.1f*(arg1->Rank() - arg2->Rank())); 
}

bool gTestShader( IEntityDSG* arg1, IEntityDSG* arg2 )
{
    if( arg1->mShaderUID < arg2->mShaderUID )
        return true;
    else
        return false;
	//return (int)(arg1->GetShaderUID() - arg2->GetShaderUID()); 
}*/

#endif
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
WorldScene::WorldScene() 
: 
    mEpsilonOffset(0.01f,0.01f,0.01f),
    mRenderAll(false),
    mpStaticTree(NULL)
{
    GetEventManager()->AddListener(this,(EventEnum)(EVENT_LOCATOR+LocatorEvent::FAR_PLANE));
    mDrawDist = 200.0f;

    mpZSorts.reserve(5000);
    rTuneAssert( mpZSorts.capacity() == 5000 );

    mpZSortsPass2.reserve(5000);
    rTuneAssert( mpZSortsPass2.capacity() == 5000 );

    mShadowCastersPass1.Allocate(300);
    //mShadowCastersPass2.Allocate(300);
    mCamPlanes.Allocate(6);

    mpZSortsPassShadowCasters.reserve(300);

#ifdef DEBUGWATCH
   radDbgWatchAddUnsignedInt( &mDebugZSWalkTiming, "ZSort Walk micros", "WorldScene", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugZSAddTiming, "ZSort Add micros", "WorldScene", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugZSSortTiming, "ZSort Sort micros", "WorldScene", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugMarkTiming, "Debug Mark micros", "WorldScene", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugWalkTiming, "Debug Walk micros", "WorldScene", NULL, NULL );
   radDbgWatchAddUnsignedInt( &mDebugRenderTiming, "Debug Render micros", "WorldScene", NULL, NULL );
   
   mDebugShowTree=false;
   radDbgWatchAddBoolean(&mDebugShowTree, "ShowTree", "WorldScene", NULL, NULL);

    // toggle collision volume drawing on and off
    mDebugSimCollisionVolumeDrawing = false;
    radDbgWatchAddBoolean(&mDebugSimCollisionVolumeDrawing, "Sim Collision Volume Drawing", "Physics Debug", NULL, NULL);

    mDebugVehicleCollisionDrawing = false;
    radDbgWatchAddBoolean(&mDebugVehicleCollisionDrawing, "Vehicle Collision Volume", "Physics Debug", NULL, NULL);

    mDebugFenceCollisionVolumeDrawing = false;
    radDbgWatchAddBoolean(&mDebugFenceCollisionVolumeDrawing, "Fence Pieces in Active Area", "Physics Debug", NULL, NULL);

    mDebugSimStatsDisplay = false;
    radDbgWatchAddBoolean(&mDebugSimStatsDisplay, "Sim Stats Display", "Physics Debug", NULL, NULL);
        
    mDebugWatchNumCollisionPairs = 0;
    radDbgWatchAddInt(&mDebugWatchNumCollisionPairs, "Num Collision Pairs", "Physics Debug", NULL, NULL );
#endif
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
WorldScene::~WorldScene()
{
   GetEventManager()->RemoveListener(this,(EventEnum)(EVENT_LOCATOR+LocatorEvent::FAR_PLANE));

   int i;
/*
    for( i=mStaticIntersects.mUseSize-1; i>-1; i-- )
   {
      mStaticIntersects[i]->Release();
   }*/

#ifdef DEBUGWATCH
   radDbgWatchDelete(&mDebugMarkTiming);
   radDbgWatchDelete(&mDebugWalkTiming);
   radDbgWatchDelete(&mDebugRenderTiming);
   radDbgWatchDelete(&mDebugZSWalkTiming);
   radDbgWatchDelete(&mDebugZSAddTiming);
   radDbgWatchDelete(&mDebugZSSortTiming);
   radDbgWatchDelete(&mDebugShowTree);

   radDbgWatchDelete(&mDebugSimCollisionVolumeDrawing);
   radDbgWatchDelete(&mDebugVehicleCollisionDrawing);
   radDbgWatchDelete(&mDebugFenceCollisionVolumeDrawing);
   radDbgWatchDelete(&mDebugSimStatsDisplay);
   radDbgWatchDelete(&mDebugWatchNumCollisionPairs);
#endif

   if(mpStaticTree == NULL)
       return;

   for(i=mStaticTreeWalker.NumNodes()-1; i>-1; i--)
   {
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mDPhysElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mFenceElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mSEntityElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mSPhysElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mAnimCollElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mAnimElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mIntersectElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mPathSegmentElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mRoadSegmentElems.mUseSize == 0 );
       rTuneAssert( mStaticTreeWalker.rIthNode(i).mTrigVolElems.mUseSize == 0 );
   }
   
   mpStaticTree->ReleaseVerified();

  // mpDefaultShader->Release();
}
//========================================================================
// worldscene::
//========================================================================
//
// Description: adapted from http://www.magic-software.com , David Eberly
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::SetVisCone( rmt::Vector& irView, rmt::Vector& irPosn, float iAngleRadians )
{
    mViewVector = irView;
    mViewPosn   = irPosn;
    
    mViewSinInv = rmt::Sin(iAngleRadians);
    mViewSinSqr = mViewSinInv*mViewSinInv;
    mViewSinInv = 1.0f/mViewSinInv;
    
    mViewCosSqr = rmt::Cos(iAngleRadians);
    mViewCosSqr = mViewCosSqr * mViewCosSqr;

    //mVisUBase.Div( irView, mViewSin );
    //mVisUBase
}
//========================================================================
// worldscene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================

#ifdef RAD_VITA
#define VITA_NOT_FADING( entity ) ( (entity)->FadeAlpha() == 255 )
extern int g_gxmFadeAlpha;     // pddi/gxm/context.cpp
#else
#define VITA_NOT_FADING( entity ) true
#endif

#ifdef RAD_VITA
float g_vitaSmallThingDist = 100.0f;    // "smalldist=" in vita.cfg, code/main/win32main.cpp
// A street lamp is a small sphere around its post, but its light on the ground reaches farther: it
// went out while still on screen. So things stay until they are this far outside the view.
// The camera only asks the tree for what is within about 200 m, so tall buildings of the skyline
// came out of nothing when the player got that close. Things with a bounding sphere this big, in
// metres, go to the root of the tree, which is always asked. 0 = off, "landmark=" in vita.cfg.
float g_vitaLandmarkRadius = 30.0f;
static void VitaPlaceFailed( const char* kind )
{
    static int s_failures = 0;
    if( ++s_failures <= 32 )
    {
        rReleasePrintf( "[tree] no room for a %s, it is left out of the world (%d so far)\n", kind, s_failures );
    }
}
float g_vitaCullMargin = 4.0f;          // "cullmargin=" in vita.cfg, metres

// The cone below is as wide as the screen and just as high, so a 16:9 view draws a lot above and
// below the picture. "framecull=0" in vita.cfg leaves it at the cone.
bool g_vitaFrameCull = false;
int g_vitaPixCull = 0;          // "pixcull=" in vita.cfg: solid things smaller than this radius on screen, in pixels
static float s_pixPerUnit = 800.0f;     // radius / distance to pixels on the 960 wide screen
static unsigned s_pixUnder2 = 0, s_pixUnder4 = 0, s_pixUnder8 = 0;
bool g_vitaOcclusion = false;   // "occlusion=1" in vita.cfg
bool g_vitaOcclSort = true;     // "occlsort=0" in vita.cfg: sorted by material alone, as in the original
float g_vitaOcclNear = 50.0f;   // "occlnear=" in vita.cfg, metres: closer than this is always drawn, a late pop would show
static unsigned s_occlQueries = 0, s_occlFoundHidden = 0, s_occlSkipped = 0, s_occlFrames = 0;
static float s_planeInside[4] = { 1.0f, 1.0f, 1.0f, 1.0f };     // which side of each plane is inside
static unsigned s_cullInCone = 0, s_cullOutOfFrame = 0, s_cullBeyondDrawDist = 0, s_cullFrames = 0;
#endif

#ifdef RAD_VITA
// Only asked for solid objects: a particle system says it has a radius of 1 m, whatever it covers.
// The side planes already have the margin in them.
bool WorldScene::VitaInFrame( const rmt::Vector& irCenter, float iRadius )
{
    ++s_cullInCone;
    {
        // There are no lower detail models of the world: what is only a few pixels is left out.
        rmt::Vector toCamera;
        toCamera.Sub( mViewPosn, irCenter );
        const float distSqr = toCamera.MagnitudeSqr();
        const float radiusPx = iRadius * s_pixPerUnit;
        const float radiusPxSqr = radiusPx * radiusPx;
        if( radiusPxSqr < 64.0f * distSqr )
        {
            ++s_pixUnder8;
            if( radiusPxSqr < 16.0f * distSqr )
            {
                ++s_pixUnder4;
                if( radiusPxSqr < 4.0f * distSqr ) { ++s_pixUnder2; }
            }
            if( g_vitaPixCull > 0 && radiusPxSqr < (float)( g_vitaPixCull * g_vitaPixCull ) * distSqr )
            {
                return false;
            }
        }
    }
    const rmt::Vector4& farPlane = mCamPlanes[ 4 ];
    if( farPlane.x * irCenter.x + farPlane.y * irCenter.y + farPlane.z * irCenter.z + farPlane.w > iRadius )
    {
        ++s_cullBeyondDrawDist;     // only counted for now
    }
    for( int side = 0; side < 4; side++ )
    {
        const rmt::Vector4& plane = mCamPlanes[ side ];
        const float at = ( plane.x * irCenter.x + plane.y * irCenter.y + plane.z * irCenter.z + plane.w ) * s_planeInside[ side ];
        if( at < -iRadius )
        {
            ++s_cullOutOfFrame;
            return !g_vitaFrameCull;
        }
    }
    return true;
}
#endif

bool WorldScene::IsSphereInCone( rmt::Vector& irCenter, float iRadius )
{
    if ( mRenderAll )
    {
        return true;
    }

    // nv:  cull small things that are far away
    const float SMALL_THING = 0.85f;
#ifdef RAD_VITA
    // Hydrants and the like. The original value of 4000 (63 m, whatever its comment said) makes them
    // appear out of nothing right in front of the player. From here they are a few pixels.
    const float SMALL_THING_CULL_DIST_SQR = g_vitaSmallThingDist * g_vitaSmallThingDist;
#else
    const float SMALL_THING_CULL_DIST_SQR = 4000.0f;
#endif
    rmt::Vector v;
    v.Sub(mViewPosn, irCenter);
    if( (iRadius < SMALL_THING) && (v.MagnitudeSqr() > SMALL_THING_CULL_DIST_SQR) )
    {
        return false;
    }

#ifdef RAD_VITA
    iRadius += g_vitaCullMargin;
#endif

    float Dsqr, e;
    rmt::Vector bigD, temp1, temp2;
    temp2 = mViewPosn; 
    
    temp1 = mViewVector;
    temp1.Scale(iRadius*mViewSinInv);

    temp2.Sub(temp1);
    
    bigD.Sub(irCenter,temp2); 
    Dsqr = bigD.Dot(bigD);
    
    e = mViewVector.Dot(bigD);

    if( e>0.0f && e*e >= Dsqr*mViewCosSqr )
    {
        bigD.Sub(irCenter,mViewPosn);
        Dsqr = bigD.Dot(bigD);
        e = -1.0f*mViewVector.Dot(bigD);

        if(e>0.0f && e*e > Dsqr*mViewSinSqr)
        {
            if( Dsqr > iRadius*iRadius )
            {
                return false;
            }
        }
        return true;
    }
    return false;

}
//========================================================================
// worldscene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::HandleEvent( EventEnum id, void* pEventData )
{
    if(id==EVENT_LOCATOR+LocatorEvent::FAR_PLANE)
    {
        unsigned int newDrawDist = ((EventLocator*)pEventData)->GetData();
 
        if(((EventLocator*)pEventData)->GetPlayerID()<1)
        {
            if(((EventLocator*)pEventData)->GetPlayerEntered())
            {
                mDrawDist = rmt::LtoF(newDrawDist);
            }
            else //exit 
            {
                mDrawDist = 200.0f;
            }
        }
        return;
    } 
    rAssert(false);
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::SetTree( SpatialTree* ipSpatialTree )
{
   rAssert( IsPreTreeGen() );
   mpStaticTree = ipSpatialTree;
   mpStaticTree->AddRef();
   mStaticTreeWalker.SetToRoot( *mpStaticTree );
   mStaticTreeWalker.AndTree(0x00000000);
   GenerateSpatialReps();
}
////////////////////////////////////////////////////////////////////
// Init will intialise the WorldScene Object:
//    -Tells WorldScene at maximum how many nRenderables will be 
//       Add'ed
//    -Is a neccessary precursor to any and all other calls to this
//       object
////////////////////////////////////////////////////////////////////
void WorldScene::Init( int nRenderables )
{
   rAssert( IsPreTreeGen() );

/*
   mStaticGeos.Allocate( nRenderables );
   mStaticIntersects.Allocate( nRenderables );
   mStaticEntities.Allocate( nRenderables );
   mStaticPhys.Allocate( nRenderables );
   */
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
void WorldScene::Add( tGeometry* pGeometry )
{
//   pGeometry->AddRef();
//   if( IsNotInScene( pGeometry ))
//   {
//      mStaticGeos.Add(pGeometry);
//   }

   if( IsPostTreeGen() )
   {
      //todo: uncomment and fix control flow
    //  PlaceStaticGeo( pGeometry );
   }
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
void WorldScene::Add( IntersectDSG* ipIntersectDSG )
{
//   ipIntersectDSG->AddRef();
///   if( IsNotInScene( ipIntersectDSG ))
   {
//      mStaticIntersects.Add(ipIntersectDSG);

//      if( IsPostTreeGen() )
   rAssert( IsPostTreeGen() );
      {
         Place( ipIntersectDSG );
      }
   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( StaticPhysDSG* ipStaticPhysDSG )
{
   ipStaticPhysDSG->AddRef();
//   if( IsNotInScene( ipStaticPhysDSG ))
//   {
//      mStaticPhys.Add(ipStaticPhysDSG);

   rAssert( IsPostTreeGen() );
//      if( IsPostTreeGen() )
      {
         Place( ipStaticPhysDSG );
      }
//  }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( StaticEntityDSG* ipStaticEntityDSG )
{
//   if( IsNotInScene( ipStaticEntityDSG ))
//   {
//      mStaticEntities.Add(ipStaticEntityDSG);

   bool isPostTreeGen = IsPostTreeGen();
   rAssert( isPostTreeGen );
    if( IsPostTreeGen() )
    {
        ipStaticEntityDSG->AddRef();
        Place( ipStaticEntityDSG );
    }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( FenceEntityDSG*  ipFenceEntityDSG )
{
   ipFenceEntityDSG->AddRef();
//   if( IsNotInScene( ipStaticEntityDSG ))
//   {
//      mFenceEntities.Add(ipFenceEntityDSG );

   rAssert( IsPostTreeGen() );
//      if( IsPostTreeGen() )
      {
         Place( ipFenceEntityDSG );
      }
//   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( AnimCollisionEntityDSG*    ipAnimCollDSG )
{
   ipAnimCollDSG->AddRef();

   rAssert( IsPostTreeGen() );

   Place( ipAnimCollDSG );
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( AnimEntityDSG*    ipAnimDSG )
{
   ipAnimDSG->AddRef();

   rAssert( IsPostTreeGen() );

   Place( ipAnimDSG );
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( DynaPhysDSG*               ipDynaPhysDSG )
{
   ipDynaPhysDSG->AddRef();

   rAssert( IsPostTreeGen() );

   Place( ipDynaPhysDSG );
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( TriggerVolume*             ipTriggerVolume )
{
    ipTriggerVolume->AddRef();

    rAssert(IsPostTreeGen());

    Place( ipTriggerVolume );
}

////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( RoadSegment*           ipRoadSegment )
{
    ipRoadSegment->AddRef();

    rAssert(IsPostTreeGen());

    Place( ipRoadSegment );
}
////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Add( PathSegment*           ipPathSegment )
{
    ipPathSegment->AddRef();

    rAssert(IsPostTreeGen());

    Place( ipPathSegment );
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
void WorldScene::GenerateSpatialReps()
{
/*
   mpDefaultShader = p3d::find<tShader>("OakL_m");
   tTexture* pTexture = p3d::find<tTexture>("OakL.bmp");
   for(int j=mStaticGeos.mUseSize-1; j>-1; j--)
   {
      for( int i=mStaticGeos[j]->GetNumPrimGroup()-1; i>-1; i-- )
      {
//         mStaticGeos[j]->GetShader(i)->SetTexture(PDDI_SP_BASETEX, pTexture);
         if( mStaticGeos[j]->GetShader(i)->GetType() == mpDefaultShader->GetType() )
         {
        //    mStaticGeos[j]->SetShader(i, mpDefaultShader);
         }
      }
   }
*/
   if( IsPreTreeGen() )
   {
      GenerateStaticTree();
   }

   //temporary to ignore weird bounding values
   mStaticTreeWalker.rBBox().mBounds.mMin.y = -200.0f;
   mStaticTreeWalker.rBBox().mBounds.mMax.y = 100.0f;
   mStaticTreeWalker.BuildBBoxes(mStaticTreeWalker.rBBox());

   PopulateStaticTree();
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::Move( rmt::Box3D& irOldBBox, IEntityDSG* ipEDSG )
{  
    /*
   if(RemovePlace(ipEDSG, mStaticTreeWalker.rIthNode(0)))
       return;

   BoxPts aBBox;
   
   aBBox.mBounds.mMin.SetTo(irOldBBox.low);
   aBBox.mBounds.mMax.SetTo(irOldBBox.high);
   aBBox.mBounds.mMin.Add(mEpsilonOffset);
   aBBox.mBounds.mMax.Sub(mEpsilonOffset);

   if(RemovePlace(ipEDSG, mStaticTreeWalker.rSeekNode( aBBox )))
       return;
*/
    if(ipEDSG->mpSpatialNode)
    {
        if(RemovePlace(ipEDSG, *(ipEDSG->mpSpatialNode) ))
            return;
    }

   rTunePrintf("Move: Can't find ipEDSG: %s, which needs to be moved in the DSG\n", ipEDSG->GetName());
//   rAssert(false);
}

bool WorldScene::RemovePlace(IEntityDSG* ipEDSG, SpatialNode& irNode)
{
   int i;
   for(i=irNode.mDPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mDPhysElems[i] )
      {
         irNode.mDPhysElems.Remove(i);
         Place((DynaPhysDSG*)(ipEDSG));
         return true;
      }
   }
   for(i=irNode.mAnimCollElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mAnimCollElems[i] )
      {
         irNode.mAnimCollElems.Remove(i);
         Place((AnimCollisionEntityDSG*)(ipEDSG));
         return true;
      }
   }
   for(i=irNode.mAnimElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mAnimElems[i] )
      {
         irNode.mAnimElems.Remove(i);
         Place((AnimEntityDSG*)(ipEDSG));
         return true;
      }
   }
    

   for(i=irNode.mTrigVolElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mTrigVolElems[i] )
      {
         irNode.mTrigVolElems.Remove(i);
         Place((TriggerVolume*)(ipEDSG));
         return true;
      }
   }
   for(i=irNode.mSEntityElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mSEntityElems[i] )
      {
         irNode.mSEntityElems.Remove(i);
         Place((StaticEntityDSG*)(ipEDSG));
         return true;
      }
   }
   for(i=irNode.mSPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == irNode.mSPhysElems[i] )
      {
         irNode.mSPhysElems.Remove(i);
         Place((StaticPhysDSG*)(ipEDSG));
         return true;
      }
   }
   return false;
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::Remove( IEntityDSG* ipEDSG )
{
   //if( RemoveFromLeaf(ipEDSG) )
   //    return;
   
   int i;
/*   BoxPts aBBox;
   rmt::Box3D Box3DBBox;
   ipEDSG->GetBoundingBox(&Box3DBBox);
   
   aBBox.mBounds.mMin.SetTo(Box3DBBox.low);
   aBBox.mBounds.mMax.SetTo(Box3DBBox.high);
   aBBox.mBounds.mMin.Add(mEpsilonOffset);
   aBBox.mBounds.mMax.Sub(mEpsilonOffset);

   SpatialNode& rNode = mStaticTreeWalker.rSeekNode( aBBox );
*/
   rAssert(ipEDSG->mpSpatialNode);
   if(!ipEDSG->mpSpatialNode)
   {
       return;
   }
   SpatialNode& rNode = *(ipEDSG->mpSpatialNode);
   ipEDSG->mpSpatialNode = NULL;

   for(i=rNode.mDPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mDPhysElems[i] )
      {
         rNode.mDPhysElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mSEntityElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mSEntityElems[i] )
      {
         rNode.mSEntityElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mSPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mSPhysElems[i] )
      {
         rNode.mSPhysElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mIntersectElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mIntersectElems[i] )
      {
         rNode.mIntersectElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mTrigVolElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mTrigVolElems[i] )
      {
         rNode.mTrigVolElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   rTunePrintf("Remove: Can't find ipEDSG: %s, which needs to be removed in the DSG\n", ipEDSG->GetName());
   rAssert(false);
}

bool WorldScene::RemoveFromLeaf( IEntityDSG* ipEDSG )
{
   SpatialNode& rNode = mStaticTreeWalker.rIthNode(0);

   int i;
   for(i=rNode.mDPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mDPhysElems[i] )
      {
         rNode.mDPhysElems.Remove(i);
         ipEDSG->Release();
         return true;
      }
   }
   for(i=rNode.mSEntityElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mSEntityElems[i] )
      {
         rNode.mSEntityElems.Remove(i);
         ipEDSG->Release();
         return true;
      }
   }
   for(i=rNode.mAnimCollElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mAnimCollElems[i] )
      {
         rNode.mAnimCollElems.Remove(i);
         ipEDSG->Release();
         return true;
      }
   }
   for(i=rNode.mAnimElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mAnimElems[i] )
      {
         rNode.mAnimCollElems.Remove(i);
         ipEDSG->Release();
         return true;
      }
   }
   return false;
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::RemoveQuietFail( IEntityDSG* ipEDSG )
{
   //if( RemoveFromLeaf(ipEDSG) )
   //    return;

   int i;
   /*
   BoxPts aBBox;
   rmt::Box3D Box3DBBox;
   ipEDSG->GetBoundingBox(&Box3DBBox);
   
   aBBox.mBounds.mMin.SetTo(Box3DBBox.low);
   aBBox.mBounds.mMax.SetTo(Box3DBBox.high);
   aBBox.mBounds.mMin.Add(mEpsilonOffset);
   aBBox.mBounds.mMax.Sub(mEpsilonOffset);
    
   SpatialNode& rNode = mStaticTreeWalker.rSeekNode( aBBox );
   */
   if(!ipEDSG->mpSpatialNode)
   {
       rDebugPrintf("RemoveQuietFail: Can't find ipEDSG: %s, which needs to be moved in the DSG\n", ipEDSG->GetName());
       return;
   }

   SpatialNode& rNode = *(ipEDSG->mpSpatialNode);
   ipEDSG->mpSpatialNode = NULL;
   
   for(i=rNode.mDPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mDPhysElems[i] )
      {
         rNode.mDPhysElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mSEntityElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mSEntityElems[i] )
      {
         rNode.mSEntityElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mSPhysElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mSPhysElems[i] )
      {
         rNode.mSPhysElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mIntersectElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mIntersectElems[i] )
      {
         rNode.mIntersectElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   for(i=rNode.mTrigVolElems.mUseSize-1;i>-1;i--)
   {
      if( ipEDSG == rNode.mTrigVolElems[i] )
      {
         rNode.mTrigVolElems.Remove(i);
         ipEDSG->Release();
         return;
      }
   }
   rDebugPrintf("RemoveQuietFail: Can't find ipEDSG: %s, which needs to be moved in the DSG\n", ipEDSG->GetName());
}


//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::RenderScene( unsigned int iFilter, tPointCamera* ipCam  )
{
#ifdef TRACK_SHADERS
    static int sRenderCall=0;
    sRenderCall++;
#endif
/*
    if(!mpTempShader)
    {
        mpTempShader = p3d::find<tShader>("fire_hydrant_m");
        rReleaseAssert(mpTempShader);
    }
*/
    static int sMaxDebugGeoCount = 0;
    int DebugRenderGeoCount = 0;
    int DebugRenderNodeCount = 0;
    int DebugRenderLeafCount = 0;

//    mpZSorts.ClearUse();
//    mpZSortsPass2.ClearUse();
    
    mpZSorts.resize( 0 );
    rTuneAssert( mpZSorts.capacity() == 5000 );
    mpZSortsPass2.resize( 0 );
    rTuneAssert( mpZSortsPass2.capacity() == 5000 );
    mpZSortsPassShadowCasters.resize(0);
    rTuneAssert( mpZSortsPassShadowCasters.capacity() == 300 );

    mShadowCastersPass1.ClearUse(); 
    //mShadowCastersPass2.ClearUse(); 

    std::vector<IEntityDSG*, s2alloc<IEntityDSG*> > mSort1;
    std::vector<IEntityDSG*, s2alloc<IEntityDSG*> > mSort2;

#ifdef DEBUGWATCH

        
    // turn off Z compare if we're drawing fence pieces also
/*
    pddiCompareMode restoreZMode;
    if(mDebugFenceCollisionVolumeDrawing)
    {
        restoreZMode = p3d::pddi->GetZCompare();
        p3d::pddi->SetZCompare(PDDI_COMPARE_NONE);      
    }
*/
#endif

#ifdef TEST_WHOLE_TREE
    static int maxDPhys=0,maxSEntity=0,maxAnimColl=0,maxAnimElem=0;

    int DPhysCount=0, SEntityCount=0, AnimCollCount=0, AnimElemCount=0, temp=0;

    for( int ith=mStaticTreeWalker.NumNodes()-1; ith>-1; ith--)
    {
        SpatialNode& rCurNode = mStaticTreeWalker.rIthNode(ith);

        temp = rCurNode.mDPhysElems.mUseSize-(rCurNode.mDPhysElems.mSize-50);
        if(temp>0)
            DPhysCount+=temp;

        temp = rCurNode.mSEntityElems.mUseSize-(rCurNode.mSEntityElems.mSize-32);
        if(temp>0)
            SEntityCount+=temp;

        AnimCollCount+=rCurNode.mAnimCollElems.mUseSize;
        AnimElemCount+=rCurNode.mAnimElems.mUseSize;
    }

    if(DPhysCount > maxDPhys)
        maxDPhys = DPhysCount; 
    if(SEntityCount > maxSEntity)
        maxSEntity = SEntityCount; 
    if(AnimCollCount > maxAnimColl)
        maxAnimColl = AnimCollCount; 
    if(AnimElemCount > maxAnimElem)
        maxAnimElem = AnimElemCount; 
#endif



#ifdef RAD_VITA
    IEntityDSG::msFadeNowMs = radTimeGetMilliseconds();
#endif
    mStaticTreeWalker.SetIterFilter( iFilter );
//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
    mDebugZSAddTiming = 0;
    unsigned int t0,t1,tRenderAcc=0;
    t0 = radTimeGetMicroseconds();
    mDebugZSWalkTiming = radTimeGetMicroseconds();
#endif
BEGIN_PROFILE("list construction")

#ifdef TRACK_SHADERS
    typedef std::map< tUID, int, std::less<tUID>, s2alloc< std::pair< const tUID, int > > > UIDMap;
    UIDMap UniqueShaders;
#endif
    //Hack
#ifdef RAD_DEBUG
            if(0)//GetMissionManager()->GetCurrentMission()->GetState()==Mission::STATE_INPROGRESS && 
                 //!GetMissionManager()->InResetState() && 
                 //GetGameFlow()->GetCurrentContext() == CONTEXT_GAMEPLAY)
            {
                rmt::Matrix Trans;
                rmt::Vector testPosn;
                pddiPrimStream*  stream;

                GetAvatarManager()->GetAvatarForPlayer(0)->GetPosition(testPosn);
                Trans.Identity();
                Trans.FillTranslate(GetMissionManager()->GetCurrentMission()->GetCurrentStage()->GetObjective()->mPathStart);
                p3d::pddi->PushMatrix(PDDI_MATRIX_MODELVIEW);
	                p3d::pddi->MultMatrix(PDDI_MATRIX_MODELVIEW,&Trans);
                      stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 2);
                      stream->Colour(tColour(255,0,0));
                      stream->Coord(0.0f, 0.0f, 0.0f);
                      stream->Colour(tColour(255,0,0));
                      stream->Coord(0.0f, 2.0f, 0.0f);
                      p3d::pddi->EndPrims(stream);
                 p3d::pddi->PopMatrix(PDDI_MATRIX_MODELVIEW);
                
                Trans.Identity();
                Trans.FillTranslate(GetMissionManager()->GetCurrentMission()->GetCurrentStage()->GetObjective()->mPathEnd);
                p3d::pddi->PushMatrix(PDDI_MATRIX_MODELVIEW);
	                p3d::pddi->MultMatrix(PDDI_MATRIX_MODELVIEW,&Trans);
                      stream = p3d::pddi->BeginPrims(NULL, PDDI_PRIM_LINESTRIP, PDDI_V_C, 2);
                      stream->Colour(tColour(0,255,0));
                      stream->Coord(0.0f, 0.0f, 0.0f);
                      stream->Colour(tColour(0,255,0));
                      stream->Coord(0.0f, 2.0f, 0.0f);
                      p3d::pddi->EndPrims(stream);
                p3d::pddi->PopMatrix(PDDI_MATRIX_MODELVIEW);

            }
#endif
    rmt::Sphere ObjectSphere;
    rmt::Vector CamPosn = ipCam->GetPosition();
//////////////////////////////////////////////////////////////////////////
    //For all the visible nodes in the tree
//#ifdef DEBUGWATCH
    pddiCompareMode origZCompare = p3d::pddi->GetZCompare();
//#endif    
    DebugRenderNodeCount = 0;
    for( mStaticTreeWalker.MoveToFirst(); mStaticTreeWalker.NotDone(); mStaticTreeWalker.MoveToNext(true) )
    {
        
        DebugRenderNodeCount++;

        if(GetCheatInputSystem()->IsCheatEnabled(CHEAT_ID_SHOW_TREE))
        {
            if( mStaticTreeWalker.IsCurrentLeaf() )
            {
                DebugRenderLeafCount++;
    //#ifdef DEBUGWATCH
              //  if(mDebugShowTree)
                {
                p3d::pddi->SetZCompare(PDDI_COMPARE_ALWAYS);
                    mStaticTreeWalker.DisplayCurrentBoundingBox( tColour(255,0,0) );
                p3d::pddi->SetZCompare(origZCompare);
                }
    //#endif
            }
            else
            {
    //#ifdef DEBUGWATCH
             //   if(mDebugShowTree)
                {
                p3d::pddi->SetZCompare(PDDI_COMPARE_ALWAYS);
                    mStaticTreeWalker.DisplayCurrentBoundingBox( tColour(0,0,0) );
                p3d::pddi->SetZCompare(origZCompare);
                }
    //#endif
                //rAssert( mStaticTreeWalker.pCurrent()->mSpatialElems.mSize    == 0 );
                //rAssert( mStaticTreeWalker.pCurrent()->mSpatialElems.mUseSize == 0 );
    //            rAssert( mStaticTreeWalker.pCurrent()->mIntersectElems.mSize    == 0 );
    //            rAssert( mStaticTreeWalker.pCurrent()->mIntersectElems.mUseSize == 0 );
            }
        }

        //      if( BEGIN_DEBUGINFO_SECTION( "CullDebug" ) )
        //      {
        //mStaticTreeWalker.DisplayCurrentBoundingBox( tColour(255,255,0) );
        //      }
        //      END_DEBUGINFO_SECTION; 

#if 1

//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#endif
        //For all the Used slots in the node's T array
        int i;
        //////////////////////////////////////////////////////////////////////////
        for( i= mStaticTreeWalker.rCurrent().mSEntityElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
#ifdef RAD_VITA
            if( mStaticTreeWalker.rCurrent().mSEntityElems[i]->mZoneAsleep )
            {
                continue;
            }
#endif
            mStaticTreeWalker.rCurrent().mSEntityElems[i]->GetBoundingSphere(&ObjectSphere);
            if(!IsSphereInCone(ObjectSphere.centre, ObjectSphere.radius))
                continue;

            //mStaticTreeWalker.rCurrent().mSEntityElems[i]->SetShader(mpTempShader,0);
            switch(3)//mStaticTreeWalker.rCurrent().mSEntityElems[i]->CastsShadow())
            {
            case 1:
                mShadowCastersPass1.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSEntityElems[i]);
                break;
            case 2:
                {
                    //mShadowCastersPass2.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSEntityElems[i]);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mSEntityElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPassShadowCasters.push_back( newDSGPtr );
                }
                break;
            default:
                if(!mStaticTreeWalker.rCurrent().mSEntityElems[i]->mTranslucent && VITA_NOT_FADING( mStaticTreeWalker.rCurrent().mSEntityElems[i] ) && mStaticTreeWalker.rCurrent().mSEntityElems[i]->CastsShadow()==0)
                {
                    //mpZSorts.AddUse(1);
                    //mpZSorts[mpZSorts.mUseSize-1].entityPtr = mStaticTreeWalker.rCurrent().mSEntityElems[i];
                    //mpZSorts[mpZSorts.mUseSize-1].shaderUID = mpZSorts[mpZSorts.mUseSize-1].entityPtr->GetShaderUID();
                    zSortBlah newBlah;
                    newBlah.entityPtr = mStaticTreeWalker.rCurrent().mSEntityElems[i];
                    newBlah.shaderUID = newBlah.entityPtr->GetShaderUID();
#ifdef RAD_VITA
                    if( VitaInFrame( ObjectSphere.centre, ObjectSphere.radius ) )
#endif
                    mpZSorts.push_back( newBlah ); 
                    //mpZSorts[mpZSorts.mUseSize-1]->SetRank((rmt::Vector&)ipCam->GetPosition());
                    //mSort1.push_back(mStaticTreeWalker.rCurrent().mSEntityElems[i]);
                }
                else
                {

                    //mpZSortsPass2.AddUse(1);
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mSEntityElems[i];
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                    
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mSEntityElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPass2.push_back( newDSGPtr );
                    //mSort2.push_back(mStaticTreeWalker.rCurrent().mSEntityElems[i]);
                }
                break;
            }
#else
            switch(mStaticTreeWalker.rCurrent().mSEntityElems[i]->CastsShadow())
            {
                mShadowCasters.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSEntityElems[i]);
            }
            else
            {
                mStaticTreeWalker.rCurrent().mSEntityElems[i]->Display();
            }
#endif
            DebugRenderGeoCount++;
        }
        //////////////////////////////////////////////////////////////////////////
        for( i= mStaticTreeWalker.rCurrent().mAnimCollElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
#ifdef RAD_VITA
            if( mStaticTreeWalker.rCurrent().mAnimCollElems[i]->mZoneAsleep )
            {
                continue;
            }
#endif
            mStaticTreeWalker.rCurrent().mAnimCollElems[i]->GetBoundingSphere(&ObjectSphere);
            if(!IsSphereInCone(ObjectSphere.centre, ObjectSphere.radius))
                continue;

            switch(3)//mStaticTreeWalker.rCurrent().mAnimCollElems[i]->CastsShadow())
            {
            case 1:
                mShadowCastersPass1.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimCollElems[i]);
                break;
            case 2:
                {
                    //mShadowCastersPass2.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimCollElems[i]);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mAnimCollElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPassShadowCasters.push_back( newDSGPtr );
                }
                break;
            default:
                if(!mStaticTreeWalker.rCurrent().mAnimCollElems[i]->mTranslucent && VITA_NOT_FADING( mStaticTreeWalker.rCurrent().mAnimCollElems[i] ) && mStaticTreeWalker.rCurrent().mAnimCollElems[i]->CastsShadow()==0)
                {
                    //mpZSorts.AddUse(1);
                    //mpZSorts[mpZSorts.mUseSize-1].entityPtr = mStaticTreeWalker.rCurrent().mAnimCollElems[i];
                    //mpZSorts[mpZSorts.mUseSize-1].shaderUID = mpZSorts[mpZSorts.mUseSize-1].entityPtr->GetShaderUID();
                    
                    zSortBlah newBlah;
                    newBlah.entityPtr = mStaticTreeWalker.rCurrent().mAnimCollElems[i];
                    newBlah.shaderUID = newBlah.entityPtr->GetShaderUID();
#ifdef RAD_VITA
                    if( VitaInFrame( ObjectSphere.centre, ObjectSphere.radius ) )
#endif
                    mpZSorts.push_back( newBlah );
                    //mpZSorts[mpZSorts.mUseSize-1]->SetRank(CamPosn);
                    //mSort1.push_back(mStaticTreeWalker.rCurrent().mAnimCollElems[i]);
                }
                else
                {
                    //mpZSortsPass2.AddUse(1);
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mAnimCollElems[i];
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                    
                    IEntityDSG* pDSGPtr = mStaticTreeWalker.rCurrent().mAnimCollElems[i];;
                    pDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPass2.push_back( pDSGPtr );
                    //mSort2.push_back(mStaticTreeWalker.rCurrent().mAnimCollElems[i]);
                }
            }
#else
            if(mStaticTreeWalker.rCurrent().mAnimCollElems[i]->CastsShadow())
            {
                mShadowCasters.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimCollElems[i]);
            }
            else
            {
                mStaticTreeWalker.rCurrent().mAnimCollElems[i]->Display();
            }
#endif
            DebugRenderGeoCount++;
        }
        //////////////////////////////////////////////////////////////////////////
        for( i= mStaticTreeWalker.rCurrent().mAnimElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
         //   if( mStaticTreeWalker.rCurrent().mAnimElems[i]->GetUID() == tName::MakeUID("smokecolumn") )
         //       rReleasePrintf("n");

#ifdef RAD_VITA
            if( mStaticTreeWalker.rCurrent().mAnimElems[i]->mZoneAsleep )
            {
                continue;
            }
#endif
            mStaticTreeWalker.rCurrent().mAnimElems[i]->GetBoundingSphere(&ObjectSphere);
            if(!IsSphereInCone(ObjectSphere.centre, ObjectSphere.radius))
                continue;

            switch(3)//mStaticTreeWalker.rCurrent().mAnimElems[i]->CastsShadow())
            {
            case 1:
                mShadowCastersPass1.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimElems[i]);
                break;
            case 2:
                {
                    //mShadowCastersPass2.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimElems[i]);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mAnimElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPassShadowCasters.push_back( newDSGPtr );
                }
                break;
            default:
                if(!mStaticTreeWalker.rCurrent().mAnimElems[i]->mTranslucent && VITA_NOT_FADING( mStaticTreeWalker.rCurrent().mAnimElems[i] ) && mStaticTreeWalker.rCurrent().mAnimElems[i]->CastsShadow()==0)
                {
                    //mpZSorts.AddUse(1);
                    //mpZSorts[mpZSorts.mUseSize-1].entityPtr = mStaticTreeWalker.rCurrent().mAnimElems[i];
                    //mpZSorts[mpZSorts.mUseSize-1].shaderUID = mpZSorts[mpZSorts.mUseSize-1].entityPtr->GetShaderUID();
                    
                    zSortBlah newBlah;
                    newBlah.entityPtr = mStaticTreeWalker.rCurrent().mAnimElems[i];
                    newBlah.shaderUID = newBlah.entityPtr->GetShaderUID();
#ifdef RAD_VITA
                    if( VitaInFrame( ObjectSphere.centre, ObjectSphere.radius ) )
#endif
                    mpZSorts.push_back( newBlah );
                    //mpZSorts[mpZSorts.mUseSize-1]->SetRank(CamPosn);
                    //mSort1.push_back(mStaticTreeWalker.rCurrent().mAnimElems[i]);
                }
                else
                {
                    // mpZSortsPass2.AddUse(1);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mAnimElems[i];
                    newDSGPtr->SetRank(CamPosn,mViewVector);
                    mpZSortsPass2.push_back( newDSGPtr );
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mAnimElems[i];
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                    
                    //mSort2.push_back(mStaticTreeWalker.rCurrent().mAnimElems[i]);
                }
            }
#else
            if(mStaticTreeWalker.rCurrent().mAnimElems[i]->CastsShadow())
            {
                mShadowCasters.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mAnimElems[i]);
            }
            else
            {
                mStaticTreeWalker.rCurrent().mAnimElems[i]->Display();
            }
#endif
            DebugRenderGeoCount++;
        }
        //////////////////////////////////////////////////////////////////////////
        for( i= mStaticTreeWalker.rCurrent().mDPhysElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
#ifdef DEBUGWATCH
      //      unsigned int dbwt=radTimeGetMicroseconds();
#endif
#ifdef RAD_VITA
            if( mStaticTreeWalker.rCurrent().mDPhysElems[i]->mZoneAsleep )
            {
                continue;
            }
#endif
            mStaticTreeWalker.rCurrent().mDPhysElems[i]->GetBoundingSphere(&ObjectSphere);
            if(!IsSphereInCone(ObjectSphere.centre, ObjectSphere.radius))
                continue;

            switch(mStaticTreeWalker.rCurrent().mDPhysElems[i]->CastsShadow())
            {
                /*
            case 1:
                mShadowCastersPass1.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mDPhysElems[i]);
                break;
            case 2:
                {
                    //mShadowCastersPass2.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mDPhysElems[i]);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPassShadowCasters.push_back( newDSGPtr );
                }
                break;
                */
            case 989: // <--Vehicle code
                // Dump the vehicle into the mpZSortsPassShadowCasters array so that 
                // displaysimpleshadow will be called on it, also let it slide into the
                // default case, so that it gets tossed into the normal mpZSorts (normal
                // translucent object
                {
                    IEntityDSG* object = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    rTuneAssert( dynamic_cast< Vehicle* >( object ) != NULL );
                    mShadowCastersPass1.Add( object );
                }
            default:
                if(!mStaticTreeWalker.rCurrent().mDPhysElems[i]->mTranslucent && VITA_NOT_FADING( mStaticTreeWalker.rCurrent().mDPhysElems[i] ) && mStaticTreeWalker.rCurrent().mDPhysElems[i]->CastsShadow()==0)
                {
                   // mpZSorts.AddUse(1);
                    //mpZSorts[mpZSorts.mUseSize-1].entityPtr = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    //mpZSorts[mpZSorts.mUseSize-1].shaderUID = mpZSorts[mpZSorts.mUseSize-1].entityPtr->GetShaderUID();
                    
                    zSortBlah newBlah;
                    newBlah.entityPtr = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    newBlah.shaderUID = newBlah.entityPtr->GetShaderUID();
#ifdef RAD_VITA
                    if( VitaInFrame( ObjectSphere.centre, ObjectSphere.radius ) )
#endif
                    mpZSorts.push_back( newBlah );
                    //mpZSorts[mpZSorts.mUseSize-1]->SetRank(CamPosn);
                    //mSort1.push_back(mStaticTreeWalker.rCurrent().mDPhysElems[i]);
                }
                else
                {
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    newDSGPtr->SetRank(CamPosn,mViewVector);
                    mpZSortsPass2.push_back( newDSGPtr );
                    //mpZSortsPass2.AddUse(1);
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mDPhysElems[i];
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                    //mSort2.push_back(mStaticTreeWalker.rCurrent().mDPhysElems[i]);
                }
            }
#ifdef DEBUGWATCH
     //       mDebugZSAddTiming += radTimeGetMicroseconds()-dbwt;
#endif
#else
            if(mStaticTreeWalker.rCurrent().mDPhysElems[i]->CastsShadow())
            {
                mShadowCasters.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mDPhysElems[i]);
            }
            else
            {
                mStaticTreeWalker.rCurrent().mDPhysElems[i]->Display();
            }
#endif
            DebugRenderGeoCount++;
        }
        //////////////////////////////////////////////////////////////////////////
        for( i= mStaticTreeWalker.rCurrent().mSPhysElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
#ifdef DEBUGWATCH
    //        unsigned int dbwt=radTimeGetMicroseconds();
#endif
#ifdef RAD_VITA
            if( mStaticTreeWalker.rCurrent().mSPhysElems[i]->mZoneAsleep )
            {
                continue;
            }
#endif
            mStaticTreeWalker.rCurrent().mSPhysElems[i]->GetBoundingSphere(&ObjectSphere);
            if(!IsSphereInCone(ObjectSphere.centre, ObjectSphere.radius))
                continue;

            switch(3)//mStaticTreeWalker.rCurrent().mSPhysElems[i]->CastsShadow())
            {
            case 1:
                mShadowCastersPass1.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSPhysElems[i]);
                break;
            case 2:
                {
                    //mShadowCastersPass2.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSPhysElems[i]);
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mSPhysElems[i];
                    newDSGPtr->SetRank( CamPosn, mViewVector );
                    mpZSortsPassShadowCasters.push_back( newDSGPtr );
                }
                break;
            default:
                if(!mStaticTreeWalker.rCurrent().mSPhysElems[i]->mTranslucent && VITA_NOT_FADING( mStaticTreeWalker.rCurrent().mSPhysElems[i] ) && mStaticTreeWalker.rCurrent().mSPhysElems[i]->CastsShadow()==0)
                {
                    //mpZSorts.AddUse(1);
                    //mpZSorts[mpZSorts.mUseSize-1].entityPtr = mStaticTreeWalker.rCurrent().mSPhysElems[i];
                    //mpZSorts[mpZSorts.mUseSize-1].shaderUID = mpZSorts[mpZSorts.mUseSize-1].entityPtr->GetShaderUID();
                    
                    zSortBlah newBlah;
                    newBlah.entityPtr = mStaticTreeWalker.rCurrent().mSPhysElems[i];
                    newBlah.shaderUID = newBlah.entityPtr->GetShaderUID();
#ifdef RAD_VITA
                    if( VitaInFrame( ObjectSphere.centre, ObjectSphere.radius ) )
#endif
                    mpZSorts.push_back( newBlah ); 
                    //mpZSorts[mpZSorts.mUseSize-1]->SetRank(CamPosn);
                    //mSort1.push_back(mStaticTreeWalker.rCurrent().mSPhysElems[i]);
                }
                else
                {
                    IEntityDSG* newDSGPtr = mStaticTreeWalker.rCurrent().mSPhysElems[i];
                    newDSGPtr->SetRank(CamPosn, mViewVector);
                    mpZSortsPass2.push_back( newDSGPtr );
                    //mpZSortsPass2.AddUse(1);
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mSPhysElems[i];
                    //mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                    //mSort2.push_back(mStaticTreeWalker.rCurrent().mSPhysElems[i]);
                }
            }
#ifdef DEBUGWATCH
   //         mDebugZSAddTiming += radTimeGetMicroseconds()-dbwt;
#endif
#else
            if(mStaticTreeWalker.rCurrent().mSPhysElems[i]->CastsShadow())
            {
                mShadowCasters.Add((IEntityDSG*&)mStaticTreeWalker.rCurrent().mSPhysElems[i]);
            }
            else
            {
                mStaticTreeWalker.rCurrent().mSPhysElems[i]->Display();
            }
#endif
            DebugRenderGeoCount++;
        }
/*        for( i= mStaticTreeWalker.rCurrent().mTrigVolElems.mUseSize-1; i>-1; i-- )
        {
#ifdef ZSORT_RENDER
#ifdef DEBUGWATCH
    //        unsigned int dbwt=radTimeGetMicroseconds();
#endif
            if(!mStaticTreeWalker.rCurrent().mTrigVolElems[i]->mTranslucent)
            {
                mpZSorts.AddUse(1);
                mpZSorts[mpZSorts.mUseSize-1] = mStaticTreeWalker.rCurrent().mTrigVolElems[i];
                //mpZSorts[mpZSorts.mUseSize-1]->SetRank(CamPosn);
                //mSort1.push_back(mStaticTreeWalker.rCurrent().mTrigVolElems[i]);
            }
            else
            {
                mpZSortsPass2.AddUse(1);
                mpZSortsPass2[mpZSortsPass2.mUseSize-1] = mStaticTreeWalker.rCurrent().mTrigVolElems[i];
                mpZSortsPass2[mpZSortsPass2.mUseSize-1]->SetRank(CamPosn);
                //mSort2.push_back(mStaticTreeWalker.rCurrent().mTrigVolElems[i]);
            }
#ifdef DEBUGWATCH
 //           mDebugZSAddTiming += radTimeGetMicroseconds()-dbwt;
#endif
#else
            mStaticTreeWalker.rCurrent().mTrigVolElems[i]->Display();
#endif
            DebugRenderGeoCount++;
        }*/
    }

//    rAssert(DebugRenderNodeCount<1000);
END_PROFILE("list construction")
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#ifdef ZSORT_RENDER
    #ifdef DEBUGWATCH
        mDebugZSWalkTiming = radTimeGetMicroseconds()-mDebugZSWalkTiming;
        mDebugZSSortTiming = radTimeGetMicroseconds();
    #endif
    /*
        NodeFLL* pHead = &(mpZSorts[mpZSorts.mUseSize-1]);
        for(int i=mpZSorts.mUseSize-2; i>-1; i--)
        {
            pHead = pHead->PlaceHighestFirst(&(mpZSorts[i]));
        }
	    */
    #ifndef RAD_RELEASE
    BEGIN_PROFILE("STD::sort1")
        std::sort(mSort1.begin(),mSort1.end(), gTestShader);
    END_PROFILE("STD::sort1")

    BEGIN_PROFILE("STD::sort2")
        std::sort(mSort2.begin(),mSort2.end(), gTestZ);
    END_PROFILE("STD::sort2")
    #endif

#ifndef TEST_DISTRIBUTED_SORT
    BEGIN_PROFILE("qsort1")
	    //qsort(mpZSorts.mpData, (size_t)mpZSorts.mUseSize, sizeof(zSortBlah), gShaderCompare);
#ifdef RAD_VITA
        if( g_vitaOcclusion && g_vitaOcclSort )
        {
            // Roughly front to back, so that the visibility queries see the near things that hide
            // the far ones. Bands, not exact order: within a band the materials still stay together.
            for( unsigned int i = 0; i < mpZSorts.size(); i++ )
            {
                rmt::Sphere sphere;
                mpZSorts[i].entityPtr->GetBoundingSphere( &sphere );
                rmt::Vector toCamera;
                toCamera.Sub( mViewPosn, sphere.centre );
                const float front = rmt::Sqrt( toCamera.MagnitudeSqr() ) - sphere.radius;
                mpZSorts[i].distanceBand = front < 25.0f ? 0 : front < 60.0f ? 1 : front < 120.0f ? 2 : 3;
            }
            std::sort( mpZSorts.begin(), mpZSorts.end(), gBandShaderCompare );
        }
        else
#endif
        std::sort( mpZSorts.begin(), mpZSorts.end(), gShaderCompare );
    END_PROFILE("qsort1")

    BEGIN_PROFILE("qsort2")
	    //qsort(mpZSortsPass2.mpData, (size_t)mpZSortsPass2.mUseSize, sizeof(IEntityDSG*), gZSortCompare);
        std::sort( mpZSortsPass2.begin(), mpZSortsPass2.end(), gZSortCompare );
    END_PROFILE("qsort2")
#endif
    //BEGIN_PROFILE("shadow caster zsort")
    //    std::sort( mpZSortsPassShadowCasters.begin(), mpZSortsPassShadowCasters.end(), dsgZSortObj );
    //END_PROFILE("shadow caster zsort")

    #ifdef DEBUGWATCH
        mDebugZSSortTiming = radTimeGetMicroseconds()-mDebugZSSortTiming;
    #endif

    #ifdef TRACK_SHADERS
        int avgUse=0, totalCount=0, unsortableCount;
        UIDMap::iterator it;
        if(sRenderCall==200)
        {
            for (it = UniqueShaders.begin(); it != UniqueShaders.end(); it++)
            {
                rTunePrintf("UniqueShader %d use count %d\n", (*it).first, (*it).second );
                if((*it).first != tName::MakeUID("__none__"))
                {
                    avgUse += (*it).second;
                    totalCount++;
                }
                else
                {
                    unsortableCount = (*it).second;
                }
            }
            rTunePrintf("Avg Use: %d  Total Sortable Count: %d  Unsortable Shader Count: %d Alpha Shaders %d\n", avgUse/totalCount, totalCount, unsortableCount, mpZSortsPass2.mUseSize);
        }
    #endif
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#endif
    //////////////////////////////////////////////////////////////////////////
    //Debug Collision volumes
    //////////////////////////////////////////////////////////////////////////        
        /*
        ReserveArray<StaticPhysDSG*>& rCollisionElems = mStaticTreeWalker.rCurrent().mDynamicElems;
        for( int i=rCollisionElems.mUseSize-1; i>-1; i-- )
        {
           rCollisionElems[i]->DisplayBoundingBox();

        }
        */

#ifdef DEBUGWATCH

    int activeArea = -1;

    Avatar* av = GetAvatarManager()->GetAvatarForPlayer(0);
    Vehicle* v = av->GetVehicle();
    Character* c = av->GetCharacter();
    if(v)
    {
        activeArea = v->mCollisionAreaIndex;
    }
    else if(c)
    {
        activeArea = c->GetCollisionAreaIndex();
    }
    

    // call to martin's sim display        
    if(mDebugSimCollisionVolumeDrawing && /*v &&*/ activeArea > -1)
    {
        sim::DisplayCollisionObjects(GetWorldPhysicsManager()->mCollisionManager, activeArea);
    }

    if(mDebugFenceCollisionVolumeDrawing && activeArea > -1)
    {
        GetWorldPhysicsManager()->DisplayFencesInArea(activeArea);

            
    }

    if(mDebugVehicleCollisionDrawing && v)
    {
        v->DebugDisplay();          
        v->CarDisplay(false);  
    }
    else if(v)
    {
        v->CarDisplay(true);
    }

    if(mDebugSimStatsDisplay)
    {
        // this makes things grind to a fucking halt
        sim::SimStats::DisplayStats();            
    }

    if(activeArea > -1)
    {
        mDebugWatchNumCollisionPairs = GetWorldPhysicsManager()->mCollisionManager->GetCollisionObjectPairList(activeArea)->GetSize();
    }
    else
    {
        mDebugWatchNumCollisionPairs = -1;
    }
    

    // set Z Compare back
    if(mDebugFenceCollisionVolumeDrawing)
    {
  //      p3d::pddi->SetZCompare(restoreZMode);
    }

#endif


    
//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
            t1 = radTimeGetMicroseconds();
#endif
#ifdef DEBUGWATCH
            tRenderAcc += radTimeGetMicroseconds()-t1;
#endif
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
    t1 = radTimeGetMicroseconds();
    mDebugWalkTiming = t1-t0; 
    mDebugRenderTiming = tRenderAcc; 
#endif
//////////////////////////////////////////////////////////////////////////

    if(DebugRenderGeoCount > sMaxDebugGeoCount )
      sMaxDebugGeoCount = DebugRenderGeoCount;
   //Assert: we are not drawing more geometries that we have 
   //(ie, drawing everything twice or something)
   //rAssert( DebugRenderGeoCount <= mStaticGeos.mUseSize );
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::RenderShadows()
{
    for( int i=mShadowCastersPass1.mUseSize-1; i>-1; i-- )
    {
        mShadowCastersPass1[i]->DisplayShadow();
    }
//    for( int i=mShadowCastersPass2.mUseSize-1; i>-1; i-- )
//    {
//        mShadowCastersPass2[i]->DisplayShadow();
//    }
    for(int i=mpZSortsPassShadowCasters.size()-1; i>-1; i--)
	{
		mpZSortsPassShadowCasters[i]->DisplayShadow();
	}
}

/*========================================================================
Draw simple blob shadows under a character. Don't call within the normal
shadow generator setup since the tris won't shade properly.
========================================================================*/
void WorldScene::RenderSimpleShadows( void )
{
    p3d::pddi->SetZWrite(false);
	for( int i = 0; i < mShadowCastersPass1.mUseSize; ++i )
	{
		mShadowCastersPass1[ i ]->DisplaySimpleShadow();
	}
//	for( int i = 0; i < mShadowCastersPass2.mUseSize; ++i )
//	{
//		mShadowCastersPass2[ i ]->DisplaySimpleShadow();
//	}
    for(int i=mpZSortsPassShadowCasters.size()-1; i>-1; i--)
	{
		mpZSortsPassShadowCasters[i]->DisplaySimpleShadow();
	}
    p3d::pddi->SetZWrite( true );
}
//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::RenderShadowCasters()
{
DSG_SET_PROFILE('C')
    for( int i=mShadowCastersPass1.mUseSize-1; i>-1; i-- )
    {
     //   BEGIN_PROFILE("ShadowCastPass1")
        mShadowCastersPass1[i]->Display();
     //   END_PROFILE("ShadowCastPass1")
    }

    //for( int i=mShadowCastersPass2.mUseSize-1; i>-1; i-- )
    //{
    //    mShadowCastersPass2[i]->Display();
    //}
    for(int i=mpZSortsPassShadowCasters.size()-1; i>-1; i--)
	{
		mpZSortsPassShadowCasters[i]->Display();
	}

}


void WorldScene::RenderOpaque( void )
{
#ifdef TEST_DISTRIBUTED_SORT
    BEGIN_PROFILE("qsort1")
	    //qsort(mpZSorts.mpData, (size_t)mpZSorts.mUseSize, sizeof(zSortBlah), gShaderCompare);
        std::sort( mpZSorts.begin(), mpZSorts.end(), gShaderCompare );
    END_PROFILE("qsort1")
#endif
DSG_SET_PROFILE('O')
BEGIN_PROFILE("qsort display")	
#ifdef RAD_VITA
    gxmContext* const occl = g_vitaOcclusion && !mRenderAll ? static_cast<gxmContext*>( p3d::pddi ) : NULL;
    const unsigned occlFrame = occl != NULL ? occl->OcclFrame() : 0;
#endif
 	for(int i=mpZSorts.size() - 1; i>-1; i--)
	{
//BEGIN_PROFILE("opaque inner")	
#ifdef RAD_VITA
        // Hardware visibility queries. Without waiting, the result of an earlier frame says that
        // none of an object's pixels survived the depth test. Such an object can be left out, and
        // is drawn again as soon as the last answer is in (on an even frame of its own), so that it
        // shows at most a few frames late when it comes into view.
        // The queries see what was drawn before the object, so they only ever find too much.
        if( occl != NULL )
        {
            IEntityDSG* const e = mpZSorts[i].entityPtr;
            const bool wasInView = occlFrame - e->mOcclLastSeen <= 1;
            e->mOcclLastSeen = occlFrame;
            if( !wasInView )
            {
                e->mOcclHidden = 0;     // it comes back into view: nothing is known about it
            }
            if( e->mOcclQueryFrame != 0 )
            {
                const int result = occl->OcclResult( e->mOcclQueryFrame, e->mOcclQueryIndex );
                if( result != -1 )
                {
                    if( result == 0 )
                    {
                        if( e->mOcclHidden < 255 ) { ++e->mOcclHidden; }
                        ++s_occlFoundHidden;
                    }
                    else
                    {
                        e->mOcclHidden = 0;
                    }
                    e->mOcclQueryFrame = 0;
                }
            }
            const unsigned phase = occlFrame + ( (unsigned)(size_t)e >> 4 );
            if( e->mOcclHidden >= 3 && ( e->mOcclQueryFrame != 0 || ( phase & 1 ) != 0 ) )
            {
                rmt::Sphere sphere;
                e->GetBoundingSphere( &sphere );
                rmt::Vector toCamera;
                toCamera.Sub( mViewPosn, sphere.centre );
                const float near = sphere.radius + g_vitaOcclNear;
                if( toCamera.MagnitudeSqr() > near * near )
                {
                    ++s_occlSkipped;
                    continue;
                }
            }
            // A hidden object asks on each drawing, a visible one on every 8th frame.
            if( e->mOcclQueryFrame == 0 && ( e->mOcclHidden != 0 || ( phase & 7 ) == 0 ) &&
                occl->OcclBegin( &e->mOcclQueryIndex ) )
            {
                e->mOcclQueryFrame = occlFrame;
                ++s_occlQueries;
                e->Display();
                occl->OcclEnd();
                continue;
            }
        }
#endif
		mpZSorts[i].entityPtr->Display();
#ifdef TRACK_SHADERS
            UniqueShaders[mpZSorts[i].entityPtr->GetShaderUID()]++;
#endif
//END_PROFILE("opaque inner")	
	}
END_PROFILE("qsort display")
#ifdef RAD_VITA
    if( occl != NULL && ++s_occlFrames >= 600 )
    {
        rReleasePrintf( "[occl] per frame: %u opaque things, %u queries, %u found hidden, %u left out\n",
                        (unsigned)mpZSorts.size(), s_occlQueries / s_occlFrames, s_occlFoundHidden / s_occlFrames, s_occlSkipped / s_occlFrames );
        s_occlQueries = s_occlFoundHidden = s_occlSkipped = s_occlFrames = 0;
    }
#endif
}

void WorldScene::RenderTranslucent( void )
{
    #ifdef RAD_GAMECUBE
        RefractionShader::AllowOneBufferCapture();
    #endif

#ifdef TEST_DISTRIBUTED_SORT
    BEGIN_PROFILE("qsort2")
	    //qsort(mpZSortsPass2.mpData, (size_t)mpZSortsPass2.mUseSize, sizeof(IEntityDSG*), gZSortCompare);
        std::sort( mpZSortsPass2.begin(), mpZSortsPass2.end(), gZSortCompare );
    END_PROFILE("qsort2")
#endif
DSG_SET_PROFILE('T')
BEGIN_PROFILE("qsort2 display")
    for(int i=mpZSortsPass2.size()-1; i>-1; i--)
	{
//BEGIN_PROFILE("translucent inner")	
#ifdef RAD_VITA
        if( mpZSortsPass2[i]->mDrawAfterGuts )
        {
            continue;
        }
        g_gxmFadeAlpha = mpZSortsPass2[i]->FadeAlpha();
#endif
#if defined( RAD_VITA ) && defined( RAD_LOG )
        // One mark per class: what the time of this loop is made of.
        const char* const vitaClassMark = typeid( *mpZSortsPass2[i] ).name();
        vitaProfBegin( vitaClassMark );
#endif
		mpZSortsPass2[i]->Display();
#if defined( RAD_VITA ) && defined( RAD_LOG )
        vitaProfEnd( vitaClassMark );
#endif
#ifdef RAD_VITA
        g_gxmFadeAlpha = 255;
#endif
//END_PROFILE("translucent inner")	
	}
END_PROFILE("qsort2 display")
}

#ifdef RAD_VITA
// Particles go after the cars and characters: those are drawn last and would paint over the smoke
// even where it is in front of them. Particles write no depth and still test it, so a car in front
// of the smoke hides it.
void WorldScene::RenderAfterGuts( void )
{
    for( int i = mpZSortsPass2.size() - 1; i > -1; i-- )
    {
        if( mpZSortsPass2[i]->mDrawAfterGuts )
        {
            g_gxmFadeAlpha = mpZSortsPass2[i]->FadeAlpha();
            mpZSortsPass2[i]->Display();
            g_gxmFadeAlpha = 255;
        }
    }
}
#endif

//========================================================================
// WorldScene::
//========================================================================
//
// Description: 
//
// Parameters:  None.
//
// Return:      None.
//
// Constraints: None.
//
//========================================================================
void WorldScene::Render(  unsigned int viewIndex )
{
    //mStaticTreeWalker.MarkTree( msVisible );
    mStaticTreeWalker.AndTree( msClear );
    //mStaticTreeWalker.OrTree( msVisible );
    //BEGIN_PROFILE("Mark Camera")

    tPointCamera* pCam;

//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
    unsigned int t0, t1;
#endif
//////////////////////////////////////////////////////////////////////////
    switch( viewIndex )
    {
    case 0:
        {
//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
            t0 = radTimeGetMicroseconds();
#endif
//////////////////////////////////////////////////////////////////////////
            pCam = (tPointCamera*)GetSuperCamManager()->GetSCC(0)->GetCamera();
BEGIN_PROFILE("cam viz")
            if( mRenderAll )    mStaticTreeWalker.OrTreeVis( msVisible0 );
            else                MarkCameraVisible( pCam, msVisible0 );

END_PROFILE("cam viz")
//////////////////////////////////////////////////////////////////////////
#ifdef DEBUGWATCH
            t1 = radTimeGetMicroseconds();
            mDebugMarkTiming = t1-t0;
#endif
//////////////////////////////////////////////////////////////////////////
            RenderScene( msVisible0, pCam );
            break;
        }
    case 1:
        {
            pCam = (tPointCamera*)GetSuperCamManager()->GetSCC(1)->GetCamera();
            MarkCameraVisible( pCam, msVisible1 );
            RenderScene( msVisible1, pCam );
            break;
        }
    default:
        {
            rAssertMsg(false, "Only supporting 1 or 2 players right now.");
            break;
        }
    }

    //END_PROFILE("Mark Camera")



}

////////////////////////////////////////////////////////////////////
//-------------------Private--------------------------------------//
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
void WorldScene::GenerateStaticTree()
{/*
   Bounds3f WorldBounds;
   rmt::Box3D BBox;
   int i;
   SpatialTreeFactory StaticTF;

   StaticTF.Reset( (mStaticIntersects.mUseSize+mStaticGeos.mUseSize) * 2 +2);

   mStaticGeos[0]->GetBoundingBox( &BBox ); WorldBounds.mMin.SetTo( BBox.low ); WorldBounds.mMax.SetTo( BBox.high ); 

   ///////////////////////////////////////////////////////////////////////
   // Seed the TreeFactory with data from all the drawable elements
   ///////////////////////////////////////////////////////////////////////
   for( i=mStaticGeos.mUseSize-1; i>-1; i-- )
   {
      mStaticGeos[i]->GetBoundingBox( &BBox );
      StaticTF.Seed( ((Vector3f&)BBox.low), 0 ); 
      StaticTF.Seed( ((Vector3f&)BBox.high), mStaticGeos[i]->GetNumPrimGroup() ); 
      WorldBounds.Accumulate( (Vector3f&)BBox.low );
      WorldBounds.Accumulate( (Vector3f&)BBox.high );
      //used to use low, but that'll lead to more bad volume intersects
   }
   ///////////////////////////////////////////////////////////////////////
   // Seed the TreeFactory with data from all the intersect elements
   ///////////////////////////////////////////////////////////////////////
   for( i=mStaticIntersects.mUseSize-1; i>-1; i-- )
   {
      mStaticIntersects[i]->GetBoundingBox( &BBox );
      StaticTF.Seed( ((Vector3f&)BBox.low), 0 ); 
      StaticTF.Seed( ((Vector3f&)BBox.high), mStaticIntersects[i]->GetNumPrimGroup() ); 
      WorldBounds.Accumulate( (Vector3f&)BBox.low );
      WorldBounds.Accumulate( (Vector3f&)BBox.high );
      //used to use low, but that'll lead to more bad volume intersects
   }

   //Add a coupla bounding seeds; bound correction to handle
   //p3dsplit/modified, which subdivides from the origin
   WorldBounds.mMin.x = rmt::Floor(WorldBounds.mMin.x/TODO_GRANULARITY.x)*TODO_GRANULARITY.x;
   WorldBounds.mMin.y = rmt::Floor(WorldBounds.mMin.y/TODO_GRANULARITY.y)*TODO_GRANULARITY.y;
   WorldBounds.mMin.z = rmt::Floor(WorldBounds.mMin.z/TODO_GRANULARITY.z)*TODO_GRANULARITY.z;
   StaticTF.Seed( WorldBounds.mMin, 0 );
   StaticTF.Seed( WorldBounds.mMax, 0 );

   ///////////////////////////////////////////////////////////////////////
   // TreeFactory: Grow a Tree, damn you! 
   ///////////////////////////////////////////////////////////////////////
   TODO_GRANULARITY.y = WorldBounds.mMax.y - WorldBounds.mMin.y;
   StaticTF.Generate( TODO_GRANULARITY );

   StaticTF.ExtractTree( &mpStaticTree );

   mStaticTreeWalker.SetToRoot( *mpStaticTree );*/
}
////////////////////////////////////////////////////////////////////
// TODO: this can be per-function templatized for all member types
////////////////////////////////////////////////////////////////////
void WorldScene::PopulateStaticTree()
{
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   int i;

   int max=mStaticTreeWalker.NumNodes();
#ifdef RAD_VITA
   mStaticTreeWalker.rIthNode(0).mSEntityElems.Reserve( 256 );  // the skyline, see Place
   // A node only has room for what the zones around the player put there. With zones kept by the zone cache
   // that ran out: things were left out of the world, and a dump crashed on them.
   for(i=0; i<max; i++)
   {
      SpatialNode& node = mStaticTreeWalker.rIthNode(i);
      node.mSEntityElems.Reserve( node.mSEntityElems.mUseSize * 3 );
      node.mSPhysElems.Reserve( node.mSPhysElems.mUseSize * 3 );
      node.mIntersectElems.Reserve( node.mIntersectElems.mUseSize * 3 );
      node.mDPhysElems.Reserve( node.mDPhysElems.mUseSize * 3 );
      node.mFenceElems.Reserve( node.mFenceElems.mUseSize * 3 );
      node.mAnimCollElems.Reserve( node.mAnimCollElems.mUseSize * 3 );
      node.mAnimElems.Reserve( node.mAnimElems.mUseSize * 3 );
      node.mTrigVolElems.Reserve( node.mTrigVolElems.mUseSize * 3 );
      node.mRoadSegmentElems.Reserve( node.mRoadSegmentElems.mUseSize * 3 );
      node.mPathSegmentElems.Reserve( node.mPathSegmentElems.mUseSize * 3 );
   }
#endif
   for(i=0; i<max; i++)
   {
      mStaticTreeWalker.rIthNode(i).mSEntityElems.Allocate();
      mStaticTreeWalker.rIthNode(i).mSPhysElems.Allocate();
      mStaticTreeWalker.rIthNode(i).mIntersectElems.Allocate();
      mStaticTreeWalker.rIthNode(i).mDPhysElems.Allocate(); 
      mStaticTreeWalker.rIthNode(i).mFenceElems.Allocate(); 
      mStaticTreeWalker.rIthNode(i).mAnimCollElems.Allocate(); 
      mStaticTreeWalker.rIthNode(i).mAnimElems.Allocate();
      mStaticTreeWalker.rIthNode(i).mTrigVolElems.Allocate(); 
      mStaticTreeWalker.rIthNode(i).mRoadSegmentElems.Allocate(); 
      mStaticTreeWalker.rIthNode(i).mPathSegmentElems.Allocate(); 
   }
   
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
//void WorldScene::PlaceStaticGeo( tGeometry* pGeometry )
//{
//   rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for geo to be added
//}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
void WorldScene::Place( IntersectDSG* ipIntersectDSG )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for geo to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipIntersectDSG->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.Place( (ISpatialProxyAA&)DrawableSP, mStaticEntities[i] );
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mIntersectElems.Add(ipIntersectDSG);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mIntersectElems.mUseSize >= pSpatialNode->mIntersectElems.mSize )
   {
      VitaPlaceFailed( "ground piece" );
      ipIntersectDSG->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mIntersectElems.Add(ipIntersectDSG);
#else
   pSpatialNode->mIntersectElems.Add(ipIntersectDSG);
#endif
   ipIntersectDSG->mpSpatialNode = pSpatialNode;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place(    StaticEntityDSG*  ipStaticEntity )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipStaticEntity->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.Place( (ISpatialProxyAA&)DrawableSP, mStaticEntities[i] );

    SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));

#ifdef RAD_VITA
   if( g_vitaLandmarkRadius > 0.0f )
   {
      rmt::Sphere sphere;
      ipStaticEntity->GetBoundingSphere( &sphere );
      SpatialNode* pRoot = &(mStaticTreeWalker.rIthNode(0));
      // Removal goes through mpSpatialNode. When the root is full it goes where it always went.
      if( sphere.radius >= g_vitaLandmarkRadius && pRoot != pSpatialNode && pRoot->mSEntityElems.Add(ipStaticEntity) )
      {
         ipStaticEntity->mpSpatialNode = pRoot;
         return;
      }
   }
#endif

   if(! pSpatialNode->mSEntityElems.Add(ipStaticEntity))
   {
       pSpatialNode = &(mStaticTreeWalker.rIthNode(0));
       if( ! pSpatialNode->mSEntityElems.Add(ipStaticEntity) )
       {
#ifdef RAD_VITA
           VitaPlaceFailed( "static entity" );
           ipStaticEntity->mpSpatialNode = NULL;
#endif
           rTuneAssert(false);
       }
       else
       {
           ipStaticEntity->mpSpatialNode = pSpatialNode;
       }
   }
   else
   {
       ipStaticEntity->mpSpatialNode = pSpatialNode;
   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place(      StaticPhysDSG*    ipStaticPhys )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipStaticPhys->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.Place( (ISpatialProxyAA&)DrawableSP, mStaticEntities[i] );
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mSPhysElems.Add(ipStaticPhys);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mSPhysElems.mUseSize >= pSpatialNode->mSPhysElems.mSize )
   {
      VitaPlaceFailed( "static physics object" );
      ipStaticPhys->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mSPhysElems.Add(ipStaticPhys);
#else
   pSpatialNode->mSPhysElems.Add(ipStaticPhys);
#endif
   ipStaticPhys->mpSpatialNode = pSpatialNode;

}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( DynaPhysDSG*      ipDynaPhys )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipDynaPhys->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));

   if(! pSpatialNode->mDPhysElems.Add(ipDynaPhys))
   {
       pSpatialNode = &(mStaticTreeWalker.rIthNode(0));
       if( ! pSpatialNode->mDPhysElems.Add(ipDynaPhys) )
       {
#ifdef RAD_VITA
           VitaPlaceFailed( "dynamic object" );
           ipDynaPhys->mpSpatialNode = NULL;
#endif
           rTuneAssert(false);
       }
       else
       {
           ipDynaPhys->mpSpatialNode = pSpatialNode;
       }
   }
   else
   {
       ipDynaPhys->mpSpatialNode = pSpatialNode;
   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( FenceEntityDSG*      ipFence )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipFence->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mFenceElems.Add(ipFence);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mFenceElems.mUseSize >= pSpatialNode->mFenceElems.mSize )
   {
      VitaPlaceFailed( "fence" );
      ipFence->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mFenceElems.Add(ipFence);
#else
   pSpatialNode->mFenceElems.Add(ipFence);
#endif
   ipFence->mpSpatialNode = pSpatialNode;
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( AnimCollisionEntityDSG* ipAnimColl )
{
   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipAnimColl->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   
    SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));

   if(! pSpatialNode->mAnimCollElems.Add(ipAnimColl))
   {
       pSpatialNode = &(mStaticTreeWalker.rIthNode(0));
       if( ! pSpatialNode->mAnimCollElems.Add(ipAnimColl) )
       {
#ifdef RAD_VITA
           VitaPlaceFailed( "animated collision object" );
           ipAnimColl->mpSpatialNode = NULL;
#endif
           rTuneAssert(false);
       }
       else
       {
           ipAnimColl->mpSpatialNode = pSpatialNode;
       }
   }
   else
   {
       ipAnimColl->mpSpatialNode = pSpatialNode;
   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( AnimEntityDSG* ipAnim )
{
//    if( ipAnim->GetUID() == tName::MakeUID("smokecolumn") )
//        rReleasePrintf("hjeifjijf");


   //rAssert(false);
   //This currently fails because PopulateStaticTree doesn't 
   //reserve any extra places for stuff to be added
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipAnim->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
    SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));

   if(! pSpatialNode->mAnimElems.Add(ipAnim))
   {
       pSpatialNode = &(mStaticTreeWalker.rIthNode(0));
       if( ! pSpatialNode->mAnimElems.Add(ipAnim) )
       {
#ifdef RAD_VITA
           VitaPlaceFailed( "animated object" );
           ipAnim->mpSpatialNode = NULL;
#endif
           rTuneAssert(false);
       }
       else
       {
           ipAnim->mpSpatialNode = pSpatialNode;
       }
   }
   else
   {
       ipAnim->mpSpatialNode = pSpatialNode;
   }
}
//////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( TriggerVolume*           ipTriggerVolume )
{
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipTriggerVolume->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mTrigVolElems.Add(ipTriggerVolume);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mTrigVolElems.mUseSize >= pSpatialNode->mTrigVolElems.mSize )
   {
      VitaPlaceFailed( "trigger" );
      ipTriggerVolume->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mTrigVolElems.Add(ipTriggerVolume);
#else
   pSpatialNode->mTrigVolElems.Add(ipTriggerVolume);
#endif
   ipTriggerVolume->mpSpatialNode = pSpatialNode;
}

////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( RoadSegment*           ipRoadSegment )
{
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipRoadSegment->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mRoadSegmentElems.Add(ipRoadSegment);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mRoadSegmentElems.mUseSize >= pSpatialNode->mRoadSegmentElems.mSize )
   {
      VitaPlaceFailed( "road segment" );
      ipRoadSegment->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mRoadSegmentElems.Add(ipRoadSegment);
#else
   pSpatialNode->mRoadSegmentElems.Add(ipRoadSegment);
#endif
   ipRoadSegment->mpSpatialNode = pSpatialNode;
}

////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////
void WorldScene::Place( PathSegment*           ipPathSegment )
{
   BoxPts DrawableSP;
   rmt::Box3D  BBox;
   
   ipPathSegment->GetBoundingBox( &BBox );
   
   DrawableSP.mBounds.mMin.SetTo( BBox.low );
   DrawableSP.mBounds.mMax.SetTo( BBox.high );
   DrawableSP.mBounds.mMin.Add(mEpsilonOffset);
   DrawableSP.mBounds.mMax.Sub(mEpsilonOffset);
   
   //mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP).mPathSegmentElems.Add(ipPathSegment);
   SpatialNode* pSpatialNode = &(mStaticTreeWalker.rSeekNode((ISpatialProxyAA&)DrawableSP));
#ifdef RAD_VITA
   // This array does not check its size itself: one too many overwrites the memory behind it.
   if( pSpatialNode->mPathSegmentElems.mUseSize >= pSpatialNode->mPathSegmentElems.mSize )
   {
      VitaPlaceFailed( "path segment" );
      ipPathSegment->mpSpatialNode = NULL;
      return;
   }
   pSpatialNode->mPathSegmentElems.Add(ipPathSegment);
#else
   pSpatialNode->mPathSegmentElems.Add(ipPathSegment);
#endif
   ipPathSegment->mpSpatialNode = pSpatialNode;
}

////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
bool WorldScene::IsPreTreeGen()
{
   if( mpStaticTree == NULL )
      return true;
   else
      return false;
}
////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////
bool WorldScene::IsPostTreeGen()
{
   if( mpStaticTree == NULL )
      return false;
   else
      return true;
}
#include <render/Culling/SphereSP.h>
#include <p3d/debugdraw.hpp>
////////////////////////////////////////////////////////////////////
// TODO: Move this type of functionality elsewhere
//       I think WorldScene Should just maintain the data interfaces
//       and management, not state of said data nor manipulation 
//       thereof
///////////////////////////////////////////////////////////////////
//static tShader* spDefaultShader = NULL;
void WorldScene::MarkCameraVisible( tPointCamera* pCam, unsigned int iFilter )
{


   //HexahedronP ViewVolSP;
#if 1
   SphereSP ViewVolSP;
   Vector3f CamPosn, ViewVector, FarPlaneExtentVect;
   pCam->GetPosition(&CamPosn);
   pCam->GetTarget(&ViewVector); ViewVector.Sub( CamPosn ); ViewVector.Normalize(); 

   SetVisCone( ViewVector, CamPosn, pCam->GetFieldOfView()/1.87f );//this tweak was done to account for the pop-in along the diagonal, especially evident in the interiors//0.8726645f ); // 50 degrees 1.745329f ); //100 degrees

   ViewVector.Mult( mDrawDist / 2.0f );
   ViewVector.Add( CamPosn );
   ViewVolSP.SetTo( ViewVector, mDrawDist / 2.0f );
#ifdef RAD_VITA
   // The planes for IsSphereInCone. The code below that builds them for the tree is compiled out.
   {
      const float realFar = pCam->GetFarPlane();
      pCam->SetFarPlane( mDrawDist );
      BuildFrustumPlanes( pCam, mCamPlanes );
      pCam->SetFarPlane( realFar );
      {
         float fov, aspect;
         pCam->GetFOV( &fov, &aspect );
         s_pixPerUnit = 480.0f / rmt::Tan( fov * 0.5f );
      }
      const rmt::Matrix& camToWorld = pCam->GetCameraToWorldMatrix();
      rmt::Vector ahead = camToWorld.Row( 3 );
      ahead.ScaleAdd( 10.0f, camToWorld.Row( 2 ) );
      for( int side = 0; side < 4; side++ )
      {
         rmt::Vector4& plane = mCamPlanes[ side ];
         const float at = plane.x * ahead.x + plane.y * ahead.y + plane.z * ahead.z + plane.w;
         plane.w += at >= 0.0f ? g_vitaCullMargin : -g_vitaCullMargin;
         s_planeInside[ side ] = at >= 0.0f ? 1.0f : -1.0f;
      }
      if( ++s_cullFrames >= 600 )
      {
         rReleasePrintf( "[cull] per frame: %u solid things in the cone, %u of them out of the picture, %u past the draw distance (%.0f m), framecull=%d; radius under 2/4/8 pixels: %u/%u/%u, pixcull=%d\n",
                         s_cullInCone / s_cullFrames, s_cullOutOfFrame / s_cullFrames, s_cullBeyondDrawDist / s_cullFrames, mDrawDist, g_vitaFrameCull ? 1 : 0,
                         s_pixUnder2 / s_cullFrames, s_pixUnder4 / s_cullFrames, s_pixUnder8 / s_cullFrames, g_vitaPixCull );
         s_cullInCone = s_cullOutOfFrame = s_cullBeyondDrawDist = s_cullFrames = 0;
         s_pixUnder2 = s_pixUnder4 = s_pixUnder8 = 0;
      }
   }
#endif


   //comment this out
   //if(spDefaultShader==NULL) 
   //   spDefaultShader = new tShader("simple");
   //P3DDrawSphere(ViewVolSP.mRadius,ViewVolSP.mCenter,*spDefaultShader,tColour(255,255,0));
#endif
//   tPointCamera* pCam  = (tPointCamera*)p3d::context->GetView()->GetCamera();
//     tPointCamera* pCam  = (tPointCamera*)GetSuperCamManager()->GetSCC(0)->GetCamera();
#if 0

   rmt::Vector4 WorldPlane;
//   float WorldPlaneNorm;


   //Do a hackey box approximation to the frustum, 
   //cuz tCamera doesn't properly provide a decent projection matrix


   Bounds3f FrustumBBox;
   Vector3f CamPosn, ViewVector, FarPlaneExtentVect;
   pCam->GetPosition(&CamPosn);
   pCam->GetTarget(&ViewVector); ViewVector.Sub( CamPosn ); ViewVector.Normalize(); //ViewVector.Mult( 200.0f );

   FarPlaneExtentVect.CrossProduct(ViewVector,rmt::Vector(0.0f,1.0f,0.0f));
   FarPlaneExtentVect.Mult( mDrawDist ); //200
   //ViewVector.Mult(7.0f);
   // ViewVector.Mult(16.0f); cut back for new art
   //ViewVector.Mult(4.0f); 
   ViewVector.Mult(mDrawDist); 

   FrustumBBox.mMin = CamPosn; FrustumBBox.mMin.y -= 10.0f;
   FrustumBBox.mMax = CamPosn;

   FrustumBBox.Accumulate( CamPosn.x + ViewVector.x + FarPlaneExtentVect.x,
                           CamPosn.y + ViewVector.y + FarPlaneExtentVect.y,
                           CamPosn.z + ViewVector.z + FarPlaneExtentVect.z );

   FrustumBBox.Accumulate( CamPosn.x + ViewVector.x - FarPlaneExtentVect.x,
                           CamPosn.y + ViewVector.y - FarPlaneExtentVect.y,
                           CamPosn.z + ViewVector.z - FarPlaneExtentVect.z );

   ViewVolSP.SetPlane( HexahedronP::msBack,     0.0f, -1.0f,  0.0f,         FrustumBBox.mMin.y);
   ViewVolSP.SetPlane( HexahedronP::msFront,    0.0f,  1.0f,  0.0f, (-1.0f)*FrustumBBox.mMax.y);
   ViewVolSP.SetPlane( HexahedronP::msLeft,    -1.0f,  0.0f,  0.0f,         FrustumBBox.mMin.x);
   ViewVolSP.SetPlane( HexahedronP::msRight,    1.0f,  0.0f,  0.0f, (-1.0f)*FrustumBBox.mMax.x);
   ViewVolSP.SetPlane( HexahedronP::msTop,      0.0f,  0.0f, -1.0f,         FrustumBBox.mMin.z);
   ViewVolSP.SetPlane( HexahedronP::msBottom,   0.0f,  0.0f,  1.0f, (-1.0f)*FrustumBBox.mMax.z);

//#else
   // ViewVector.Mult(16.0f); cut back for new art
   float tmpFar = pCam->GetFarPlane();
   pCam->SetFarPlane(mDrawDist);
   //pCam->SetFarPlane(175.0f);
   BuildFrustumPlanes( pCam, mCamPlanes );
   pCam->SetFarPlane(tmpFar);
#ifdef RAD_VITA
   {
      // The side planes move out by the same margin. Which side is inside depends on the sign of
      // the plane, so it is measured with a point straight ahead of the camera.
      const rmt::Matrix& camToWorld = pCam->GetCameraToWorldMatrix();
      rmt::Vector ahead = camToWorld.Row( 3 );
      ahead.ScaleAdd( 10.0f, camToWorld.Row( 2 ) );
      for( int side = 0; side < 4; side++ )
      {
         rmt::Vector4& plane = mCamPlanes[ side ];
         const float at = plane.x * ahead.x + plane.y * ahead.y + plane.z * ahead.z + plane.w;
         plane.w += at >= 0.0f ? g_vitaCullMargin : -g_vitaCullMargin;
      }
   }
#endif
   
   ViewVolSP.SetPlane( HexahedronP::msTop,     mCamPlanes[0].x, mCamPlanes[0].y, mCamPlanes[0].z, mCamPlanes[0].w );
   ViewVolSP.SetPlane( HexahedronP::msLeft,    mCamPlanes[1].x, mCamPlanes[1].y, mCamPlanes[1].z, mCamPlanes[1].w );
   ViewVolSP.SetPlane( HexahedronP::msBottom,  mCamPlanes[2].x, mCamPlanes[2].y, mCamPlanes[2].z, mCamPlanes[2].w );
   ViewVolSP.SetPlane( HexahedronP::msRight,   mCamPlanes[3].x, mCamPlanes[3].y, mCamPlanes[3].z, mCamPlanes[3].w );
   ViewVolSP.SetPlane( HexahedronP::msBack,    mCamPlanes[4].x, mCamPlanes[4].y, mCamPlanes[4].z, mCamPlanes[4].w );
   ViewVolSP.SetPlane( HexahedronP::msFront,   mCamPlanes[5].x, mCamPlanes[5].y, mCamPlanes[5].z, mCamPlanes[5].w );
#endif


   //ViewVolSP.GeneratePoints();
   //mStaticTreeWalker.MarkAll( ViewVolSP, iFilter );
   mStaticTreeWalker.MarkAllSphere( ViewVolSP, iFilter );
}
/////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////
void WorldScene::BuildFrustumPlanes( tPointCamera* pCam, FixedArray<rmt::Vector4>& orCamPlanes )
{
    rAssert( orCamPlanes.mSize == 6 );

//    tPointCamera* pCam = (tPointCamera*)p3d::context->GetView()->GetCamera();
//    tPointCamera* pCam  = (tPointCamera*)GetSuperCamManager()->GetSCC(0)->GetCamera();
    const rmt::Matrix* pCam2WorldMat = &pCam->GetCameraToWorldMatrix();

    //
    //  Build the camera volume (written by Nigel Brooke)
    //

//    Plane cameraPlanes[6];

    // camera parameters
    float fov, aspect; pCam->GetFOV( &fov, &aspect ); //fov = fov/1.5f;
    //float aspect = 1.333f; // TODO : shouldn't be hardcoded

    float nearPlane = pCam->GetNearPlane();
    float farPlane = pCam->GetFarPlane();

    // build some useful points
    rmt::Vector eye  = pCam2WorldMat->Row(3); // eye point
    rmt::Vector look = pCam2WorldMat->Row(2); // look direciton, normal for far plane
    rmt::Vector toFarPlane = look; toFarPlane.Scale(farPlane);
    rmt::Vector onFarPlane = eye; onFarPlane.Add(toFarPlane);  // a point on the far plane

    rmt::Vector lookInv = pCam2WorldMat->Row(2); lookInv.Scale(-1);
    // toNearPlane offset by a small value to avoid some artifacts
    rmt::Vector toNearPlane = look; toNearPlane.Scale(nearPlane - 0.00001f);
    rmt::Vector onNearPlane = eye; onNearPlane.Add(toNearPlane);

    // find surface normals for left and right clipping pplanes
    // first get camera space vectors
    rmt::Vector tmpr(rmt::Cos(fov/2), 0, -rmt::Sin(fov/2));  // right vector is 'rotated' by fov/2
    rmt::Vector tmpl = tmpr; tmpl.Scale(-1,1,1);   // left vector is inverse of right

    // then pass points through camera matrix to get world space vectors
    rmt::Vector right, left;
    pCam2WorldMat->RotateVector(tmpr, &right);
    pCam2WorldMat->RotateVector(tmpl, &left);

    // find surface normals for top and bottom clipping planes, just like left and right
    // get camera space vectors
    rmt::Vector tmpu(0, rmt::Cos(fov/2), -rmt::Sin(fov/2));
    tmpu.Scale(1, aspect, 1);
    tmpu.Normalize();
    rmt::Vector tmpd = tmpu; tmpd.Scale(1,-1,1);

    // tranform to worldspace
    rmt::Vector up,  down;
    pCam2WorldMat->RotateVector(tmpu, &up);
    pCam2WorldMat->RotateVector(tmpd, &down);

    // set the planes using point in plane and normal format
    // 'eye' is on all clipping planes except far and near
    orCamPlanes[0].Set(up.x,      up.y,      up.z,      -up.DotProduct(eye));
    orCamPlanes[1].Set(left.x,    left.y,    left.z,    -left.DotProduct(eye));
    orCamPlanes[2].Set(down.x,    down.y,    down.z,    -down.DotProduct(eye));
    orCamPlanes[3].Set(right.x,   right.y,   right.z,   -right.DotProduct(eye));
    orCamPlanes[4].Set(look.x,    look.y,    look.z,    -look.DotProduct(onFarPlane));
    orCamPlanes[5].Set(lookInv.x, lookInv.y, lookInv.z, -lookInv.DotProduct(onNearPlane));
}
