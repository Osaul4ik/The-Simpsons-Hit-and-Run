//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenOptions
//
// Description: Implementation of the CGuiScreenOptions class.
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/07/04      TChu        Created for SRR2
//
//===========================================================================

//===========================================================================
// Includes
//===========================================================================
#include <presentation/gui/frontend/guiscreenoptions.h>
#include <presentation/gui/frontend/guiscreenplaymovie.h>
#include <presentation/gui/guimenu.h>
#include <presentation/gui/guisystem.h>
#include <presentation/gui/guimanager.h>
#include <presentation/gui/guiscreenmessage.h>
#include <presentation/gui/guiscreenprompt.h>

#include <cheats/cheatinputsystem.h>
#include <cheats/cheats.h>
#include <memory/srrmemory.h>

#include <raddebug.hpp> // Foundation
#include <stdio.h>
#include <Layer.h>
#include <Page.h>
#include <Screen.h>
#include <Group.h>
#include <Text.h>

#ifdef RAD_PS2
    #include <main/ps2platform.h>
#endif

#ifdef RAD_VITA
#include <pddi/gxm/shaderbuild.hpp>

// code/main/win32main.cpp
int VitaGetFrameRate();
void VitaSetFrameRate( int fps );
void VitaSaveFrameRate();
#endif

//===========================================================================
// Global Data, Local Data, Local Classes
//===========================================================================

const unsigned int NUM_CHEAT_DISPLAY_LINES = 16;

#ifdef RAD_VITA
// distance between two rows of the menu, in Scrooby coordinates (0 if the page lacks the items)
static int RowSpacing( Scrooby::Group* menu )
{
    Scrooby::Text* first = menu->GetText( "Controller" );
    Scrooby::Text* second = menu->GetText( "Sound" );
    if( first == NULL || second == NULL )
    {
        return 0;
    }
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    first->GetOriginPosition( x0, y0 );
    second->GetOriginPosition( x1, y1 );
    return y1 - y0;
}
#endif
const int PROGRESSIVE_MODE_TEST_TIME = 5000; // in msec

//===========================================================================
// Public Member Functions
//===========================================================================

//===========================================================================
// CGuiScreenOptions::CGuiScreenOptions
//===========================================================================
// Description: Constructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenOptions::CGuiScreenOptions
(
	Scrooby::Screen* pScreen,
	CGuiEntity* pParent
)
:   CGuiScreen( pScreen, pParent, GUI_SCREEN_ID_OPTIONS ),
    m_pMenu( NULL ),
    m_cheatsPage( NULL ),
    m_cheatsOverlay( NULL ),
    m_startingCheatID( 0 ),
    m_elapsedProgressiveModeTestTime( -1 )
#ifdef RAD_VITA
    , m_frameRateLabel( NULL ),
    m_savedFrameRate( VitaGetFrameRate() ),
    m_compileLabel( NULL ),
    m_shaderStatus( NULL ),
    m_compileNext( -1 ),
    m_compileDone( 0 ),
    m_compileLabelShown( false ),
    m_compileFinished( false )
#endif
{
MEMTRACK_PUSH_GROUP( "CGUIScreenOptions" );
    // Retrieve the Scrooby drawing elements.
    //
    Scrooby::Page* pPage = m_pScroobyScreen->GetPage( "Options" );
    rAssert( pPage != NULL );

    // Create a menu.
    //
    m_pMenu = new CGuiMenu( this, NUM_MENU_ITEMS );
    rAssert( m_pMenu != NULL );

    // Add menu items
    //
    Scrooby::Group* pGroup = pPage->GetGroup( "Menu" );
    rAssert( pGroup != NULL );

#ifdef RAD_PC
    m_pMenu->AddMenuItem( pGroup->GetText( "Display" ) );
#endif
    m_pMenu->AddMenuItem( pGroup->GetText( "Controller" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "Sound" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "ViewMovies" ) );
    m_pMenu->AddMenuItem( pGroup->GetText( "ViewCredits" ) );

#ifndef RAD_PC
    Scrooby::Text* display = pGroup->GetText( "Display" );
    if( display != NULL )
    {
        display->SetVisible( false );
    }
#endif

#ifdef RAD_VITA
    // The PS2 "display mode" texts are smaller than the other items and their value sits a row
    // lower, so they are hidden. The frame rate is a new text in the style of the other items,
    // at the next row, showing its value and toggling on accept/left/right.
    Scrooby::Text* frameRateValue = NULL;
    const int rowSpacing = RowSpacing( pGroup );
    m_frameRateLabel = pGroup->AddTextLike( pGroup->GetText( "Controller" ), "FrameRate", 0, rowSpacing * 4 );
    if( m_frameRateLabel == NULL )
    {
        m_frameRateLabel = pGroup->GetText( "DisplayMode" );
    }
    else
    {
        pGroup->GetText( "DisplayMode" )->SetVisible( false );
    }
    if( pGroup->GetText( "DisplayMode_Value" ) != NULL )
    {
        pGroup->GetText( "DisplayMode_Value" )->SetVisible( false );
    }
    if( pGroup->GetSprite( "DisplayMode_ArrowL" ) != NULL )
    {
        pGroup->GetSprite( "DisplayMode_ArrowL" )->SetVisible( false );
    }
    if( pGroup->GetSprite( "DisplayMode_ArrowR" ) != NULL )
    {
        pGroup->GetSprite( "DisplayMode_ArrowR" )->SetVisible( false );
    }
    m_pMenu->AddMenuItem( m_frameRateLabel,
                          frameRateValue,
#else
    m_pMenu->AddMenuItem( pGroup->GetText( "DisplayMode" ),
                          pGroup->GetText( "DisplayMode_Value" ),
#endif
                          NULL,
                          NULL,
                          pGroup->GetSprite( "DisplayMode_ArrowL" ),
                          pGroup->GetSprite( "DisplayMode_ArrowR" ),
#ifdef RAD_VITA
                          SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED | ( frameRateValue == NULL ? SELECTABLE : 0 ) );
#else
                          SELECTION_ENABLED | VALUES_WRAPPED | TEXT_OUTLINE_ENABLED );
#endif

#ifdef RAD_VITA
    // Two more rows the frontend data does not have, continuing the rows' spacing, and the whole
    // menu moved by one row to stay centered.
    {
        Scrooby::Text* first = pGroup->GetText( "Controller" );
        if( first != NULL && rowSpacing != 0 )
        {
            // Controller, Sound, ViewMovies, ViewCredits, frame rate: the new ones are rows 6 and 7
            m_compileLabel = pGroup->AddTextLike( first, "CompileShaders", 0, rowSpacing * 5 );
            m_shaderStatus = pGroup->AddTextLike( first, "ShaderStatus", 0, rowSpacing * 6 );
        }
        if( m_compileLabel != NULL && m_shaderStatus != NULL )
        {
            m_pMenu->AddMenuItem( m_compileLabel );

            // The status can be long: a wider box around the same center, and a bit smaller.
            int width = 0, height = 0;
            m_shaderStatus->GetBoundingBoxSize( width, height );
            if( m_shaderStatus->GetHorizontalJustification() == Scrooby::Centre )
            {
                m_shaderStatus->SetBoundingBoxSize( width * 2, height );
                m_shaderStatus->Translate( -width / 2, 0 );
            }
            m_shaderStatus->ScaleAboutCenter( 0.8f );

            pGroup->ResetTransformation();
            pGroup->Translate( 0, -rowSpacing );

            this->UpdateShaderStatus();
        }
    }
#endif

#ifdef RAD_DEMO
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_VIEW_MOVIES, false );
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_VIEW_CREDITS, false );
#endif

/*
#ifdef RAD_RELEASE
    // hide 'view movies' in release build
    //
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_VIEW_MOVIES, false, true );
#else
    // center menu items
    //
    pGroup->ResetTransformation();
    pGroup->Translate( 0, 30 );
#endif
*/

    // TC: no more display mode menu item on PS2, this is now done during bootup
    //     w/ specific button combo pressed and a display prompt
    //
//#ifndef RAD_PS2
#ifdef RAD_VITA
    if( frameRateValue != NULL )
    {
        m_pMenu->SetSelectionValueCount( MENU_ITEM_DISPLAY_MODE, 3 );
    }
    this->UpdateFrameRateLabel();
#else
    // hide display mode menu item for non-PS2 platforms
    //
    m_pMenu->SetMenuItemEnabled( MENU_ITEM_DISPLAY_MODE, false, true );

    #ifndef RAD_PC
    // re-center menu items
    //
    pGroup->ResetTransformation();
    pGroup->Translate( 0, -30 );
    #endif
#endif
//#endif // !RAD_PS2

    // get cheats page, if included
    //
    m_cheatsPage = m_pScroobyScreen->GetPage( "Cheats" );
    if( m_cheatsPage != NULL )
    {
        m_cheatsOverlay = m_cheatsPage->GetLayerByIndex( 0 );
    }

    // register self as cheat callback
    //
    GetCheatInputSystem()->RegisterCallback( this );
MEMTRACK_POP_GROUP( "CGUIScreenOptions" );
}


//===========================================================================
// CGuiScreenOptions::~CGuiScreenOptions
//===========================================================================
// Description: Destructor.
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
CGuiScreenOptions::~CGuiScreenOptions()
{
    if( m_pMenu != NULL )
    {
        delete m_pMenu;
        m_pMenu = NULL;
    }

    // unregister self as cheat callback
    //
    GetCheatInputSystem()->UnregisterCallback( this );
}


//===========================================================================
// CGuiScreenOptions::HandleMessage
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::HandleMessage
(
	eGuiMessage message, 
	unsigned int param1,
	unsigned int param2 
)
{
    if( GetCheatInputSystem()->IsCheatEnabled( CHEAT_ID_MOTHER_OF_ALL_CHEATS ) )
    {
        if( m_cheatsOverlay != NULL )
        {
            // show/hide cheats overlay, depending on whether or not
            // cheat input is activated on any of the controllers
            //
            bool isCheatInputActivated = false;
            int numUserInputHandlers = GetGuiSystem()->GetNumUserInputHandlers();
            for( int i = 0; i < numUserInputHandlers; i++ )
            {
                if( GetGuiSystem()->GetUserInputHandler( i ) != NULL &&
                    GetCheatInputSystem()->IsActivated( i ) )
                {
                    isCheatInputActivated = true;

                    break;
                }
            }

            m_cheatsOverlay->SetVisible( isCheatInputActivated );
        }

        if( message == GUI_MSG_CONTROLLER_RIGHT &&
            GetCheatInputSystem()->IsActivated( param1 ) )
        {
            // toggle cheat list
            //
            m_startingCheatID += NUM_CHEAT_DISPLAY_LINES;
            if( m_startingCheatID >= NUM_CHEATS )
            {
                // wrap to the beginning
                m_startingCheatID = 0;
            }

            this->UpdateCheatsDisplay( m_startingCheatID );
        }
    }

    if( this->IsControllerMessage( message ) &&
        GetCheatInputSystem()->IsActivated( param1 ) )
    {
        // ignore all controller inputs when cheat input system is activated
        return;
    }

#ifdef RAD_VITA
    if( m_compileNext >= 0 )
    {
        // the menu stays on screen and cannot be left until the compiler is done
        if( this->IsControllerMessage( message ) )
        {
            return;
        }
        if( message == GUI_MSG_UPDATE && m_state == GUI_WINDOW_STATE_RUNNING )
        {
            this->UpdateShaderCompile();
        }
    }
#endif

    if( message == GUI_MSG_MENU_PROMPT_RESPONSE )
    {
        if( param1 == PROMPT_DISPLAY_PROGRESSIVE_SCAN_PS2 )
        {
            if( param2 == CGuiMenuPrompt::RESPONSE_YES )
            {
                // begin progressive scan test
                //
                this->ProgressiveModeTestBegin();
            }
            else
            {
                // don't switch display mode and return to options screen
                //
                this->ReloadScreen();
            }
        }
        else if( param1 == PROMPT_DISPLAY_PROGRESSIVE_SCAN_CONFIRM )
        {
            if( param2 == CGuiMenuPrompt::RESPONSE_YES )
            {
#ifdef RAD_PS2
                PS2Platform::GetInstance()->SetProgressiveMode( true );
#endif
                rTunePrintf( "Current display mode confirmed: PROGRESSIVE.\n" );
            }
            else
            {
                rTunePrintf( "Current display mode confirmed: INTERLACED.\n" );
            }

            // return to options screen
            //
            this->ReloadScreen();
        }
		else
			rTuneAssert(!"not handled");
    }
    else if( message == GUI_MSG_MESSAGE_UPDATE || message == GUI_MSG_PROMPT_UPDATE ) // update from message/prompt screen
    {
        if( m_elapsedProgressiveModeTestTime != -1 )
        {
            m_elapsedProgressiveModeTestTime += static_cast<int>( param1 );

            if( m_elapsedProgressiveModeTestTime > PROGRESSIVE_MODE_TEST_TIME )
            {
                this->OnProgressiveModeTestEnd();
            }
        }
    }

    if( m_state == GUI_WINDOW_STATE_RUNNING )
    {
#ifdef RAD_VITA
        if( m_frameRateLabel != NULL &&
            ( message == GUI_MSG_CONTROLLER_LEFT || message == GUI_MSG_CONTROLLER_RIGHT ) &&
            m_pMenu->GetSelection() == MENU_ITEM_DISPLAY_MODE )
        {
            this->ToggleFrameRate( message == GUI_MSG_CONTROLLER_LEFT ? -1 : 1 );
        }
#endif
        switch( message )
        {
            case GUI_MSG_MENU_SELECTION_CHANGED:
            {
#ifdef RAD_VITA
                this->SetButtonVisible( BUTTON_ICON_ACCEPT, param1 != MENU_ITEM_DISPLAY_MODE || m_frameRateLabel != NULL );
#else
                this->SetButtonVisible( BUTTON_ICON_ACCEPT, param1 != MENU_ITEM_DISPLAY_MODE );
#endif

                break;
            }
            case GUI_MSG_MENU_SELECTION_VALUE_CHANGED:
            {
#ifdef RAD_VITA
                if( param1 == MENU_ITEM_DISPLAY_MODE )
                {
                    VitaSetFrameRate( param2 == 2 ? 60 : param2 == 1 ? 50 : 30 );
                }
#else
                if( param1 == MENU_ITEM_DISPLAY_MODE )
                {
                    if( param2 == 1 ) // progressive mode
                    {
                        // display confirmation prompt
                        //
                        m_guiManager->DisplayPrompt( PROMPT_DISPLAY_PROGRESSIVE_SCAN_PS2, this );
                    }
                    else // interlaced mode
                    {
#ifdef RAD_PS2
                        PS2Platform::GetInstance()->SetProgressiveMode( false );
#endif
                    }
                }
#endif // RAD_VITA

                break;
            }
            case GUI_MSG_MENU_SELECTION_MADE:
            {
                switch( param1 )
                {
#ifdef RAD_VITA
                    case MENU_ITEM_COMPILE_SHADERS:
                    {
                        // Only ever started here: nothing compiles on its own.
                        m_compileNext = 0;
                        m_compileDone = 0;
                        m_compileLabelShown = false;
                        m_compileFinished = false;

                        break;
                    }
                    case MENU_ITEM_DISPLAY_MODE:
                    {
                        if( m_frameRateLabel != NULL )
                        {
                            this->ToggleFrameRate( 1 );
                        }

                        break;
                    }
#endif
                    case MENU_ITEM_CONTROLLER:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_CONTROLLER );

                        this->StartTransitionAnimation( 530, 560 );

                        break;
                    }
                    case MENU_ITEM_SOUND:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_SOUND );

                        this->StartTransitionAnimation( 660, 690 );

                        break;
                    }
                    case MENU_ITEM_VIEW_MOVIES:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_VIEW_MOVIES );

                        this->StartTransitionAnimation( 914, 944 );

                        break;
                    }
                    case MENU_ITEM_VIEW_CREDITS:
                    {
/*
                        rAssert( m_guiManager );
                        CGuiScreenPlayMovie* playMovieScreen = static_cast<CGuiScreenPlayMovie*>( m_guiManager->FindWindowByID( GUI_SCREEN_ID_VIEW_CREDITS ) );
                        rAssert( playMovieScreen );
                        playMovieScreen->SetMovieToPlay( MovieNames::CREDITS );
*/
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_VIEW_CREDITS );

                        this->StartTransitionAnimation( 600, 630 );

                        break;
                    }
#ifdef RAD_PC
                    case MENU_ITEM_DISPLAY:
                    {
                        m_pParent->HandleMessage( GUI_MSG_GOTO_SCREEN, GUI_SCREEN_ID_DISPLAY );

                        break;
                    }
#endif
                    default:
                    {
                        break;
                    }
                }

                break;
            }

            case GUI_MSG_CONTROLLER_BACK:
            {
                this->StartTransitionAnimation( 110, 140 );

                break;
            }

            default:
            {
                break;
            }
        }

        // relay message to menu
        if( m_pMenu != NULL )
        {
            m_pMenu->HandleMessage( message, param1, param2 );
        }
    }

	// Propogate the message up the hierarchy.
	//
	CGuiScreen::HandleMessage( message, param1, param2 );
}


//===========================================================================
// CGuiScreenOptions::OnCheatEntered
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void
CGuiScreenOptions::OnCheatEntered( eCheatID cheatID, bool isEnabled )
{
    this->UpdateCheatsDisplay( m_startingCheatID );

    if( cheatID == CHEAT_ID_MOTHER_OF_ALL_CHEATS && !isEnabled )
    {
        if( m_cheatsOverlay != NULL )
        {
            m_cheatsOverlay->SetVisible( false );
        }
    }
}


//===========================================================================
// CGuiScreenOptions::InitIntro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitIntro()
{
#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( true );
#endif

    rAssert( m_pMenu != NULL );
#ifdef RAD_VITA
    if( m_pMenu->GetSelection() == MENU_ITEM_DISPLAY_MODE && m_frameRateLabel == NULL )
#else
    if( m_pMenu->GetSelection() == MENU_ITEM_DISPLAY_MODE )
#endif
    {
        this->SetButtonVisible( BUTTON_ICON_ACCEPT, false );
    }

#ifdef RAD_VITA
    // show the current frame rate (also set by "fps=" in vita.cfg)
    //
    if( m_frameRateLabel == NULL )
    {
        m_pMenu->SetSelectionValue( MENU_ITEM_DISPLAY_MODE, VitaGetFrameRate() >= 60 ? 2 : VitaGetFrameRate() >= 50 ? 1 : 0 );
    }
    this->UpdateFrameRateLabel();
    this->UpdateShaderStatus();
#endif

#ifdef RAD_PS2
    // update current display mode
    //
    m_pMenu->SetSelectionValue( MENU_ITEM_DISPLAY_MODE,
                                PS2Platform::GetInstance()->GetProgressiveMode() ? 1 : 0 );
#endif
}


//===========================================================================
// CGuiScreenOptions::InitRunning
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitRunning()
{
}


//===========================================================================
// CGuiScreenOptions::InitOutro
//===========================================================================
// Description: 
//
// Constraints:	None.
//
// Parameters:	None.
//
// Return:      N/A.
//
//===========================================================================
void CGuiScreenOptions::InitOutro()
{
#ifndef RAD_E3
    GetCheatInputSystem()->SetEnabled( false );
#endif

#ifdef RAD_VITA
    // written when leaving the screen, not on every toggle (a memory card write takes a while)
    if( VitaGetFrameRate() != m_savedFrameRate )
    {
        VitaSaveFrameRate();
        m_savedFrameRate = VitaGetFrameRate();
    }
#endif
}

#ifdef RAD_VITA
void CGuiScreenOptions::UpdateFrameRateLabel()
{
    if( m_frameRateLabel != NULL )
    {
        const int fps = VitaGetFrameRate();
        m_frameRateLabel->SetString( 0, fps >= 60 ? "FRAME RATE 60 FPS" : fps >= 50 ? "FRAME RATE 50 FPS" : "FRAME RATE 30 FPS" );
    }
}

void CGuiScreenOptions::UpdateShaderStatus()
{
    if( m_compileLabel == NULL || m_shaderStatus == NULL )
    {
        return;
    }

    int compiled = 0;
    for( int i = 0; i < GXM_SHADER_COUNT; i++ )
    {
        if( gxmShaderIsCompiled( static_cast<gxmShaderId>( i ) ) )
        {
            compiled++;
        }
    }

    char label[64];
    if( m_compileFinished && gxmShaderCompilerMissing() )
    {
        sprintf( label, "SHADER COMPILER NOT FOUND" );
    }
    else if( m_compileFinished && m_compileDone < GXM_SHADER_COUNT )
    {
        sprintf( label, "COMPILED %d OF %d", m_compileDone, static_cast<int>( GXM_SHADER_COUNT ) );
    }
    else if( compiled == GXM_SHADER_COUNT )
    {
        sprintf( label, "SHADERS COMPILED" );
    }
    else
    {
        sprintf( label, "COMPILE SHADERS" );
    }
    m_compileLabel->SetString( 0, label );

    const bool variants = gxmShaderVariantsInUse();
    const bool skinning = gxmShaderHardwareSkinningInUse();
    if( m_compileFinished && compiled == GXM_SHADER_COUNT && !( variants && skinning ) )
    {
        m_shaderStatus->SetString( 0, "RESTART THE GAME TO USE THEM" );
    }
    else if( !variants )
    {
        m_shaderStatus->SetString( 0, "USING ORIGINAL SHADERS" );
    }
    else if( !skinning )
    {
        m_shaderStatus->SetString( 0, "USING OPTIMIZED SHADERS" );
    }
    else
    {
        m_shaderStatus->SetString( 0, "USING OPTIMIZED SHADERS AND GPU SKINNING" );
    }
}

void CGuiScreenOptions::UpdateShaderCompile()
{
    // One program per update, with the progress on screen for a frame first: a program can take
    // seconds and blocks the frame it is compiled in.
    if( !m_compileLabelShown )
    {
        char label[64];
        sprintf( label, "COMPILING %d OF %d", m_compileNext + 1, static_cast<int>( GXM_SHADER_COUNT ) );
        m_compileLabel->SetString( 0, label );
        m_compileLabelShown = true;
        return;
    }
    m_compileLabelShown = false;

    if( gxmShaderCompile( static_cast<gxmShaderId>( m_compileNext ) ) )
    {
        m_compileDone++;
    }
    m_compileNext++;

    if( gxmShaderCompilerMissing() || m_compileNext >= GXM_SHADER_COUNT )
    {
        m_compileNext = -1;
        m_compileFinished = true;
        this->UpdateShaderStatus();
    }
}

void CGuiScreenOptions::ToggleFrameRate( int direction )
{
    // 30 -> 50 -> 60 -> 30, or the other way round
    const int fps = VitaGetFrameRate();
    if( direction > 0 )
    {
        VitaSetFrameRate( fps >= 60 ? 30 : fps >= 50 ? 60 : 50 );
    }
    else
    {
        VitaSetFrameRate( fps >= 60 ? 50 : fps >= 50 ? 30 : 60 );
    }
    this->UpdateFrameRateLabel();
}
#endif


//---------------------------------------------------------------------
// Private Functions
//---------------------------------------------------------------------

void
CGuiScreenOptions::UpdateCheatsDisplay( int cheatID )
{
    if( m_cheatsPage != NULL )
    {
        // update cheats info
        //
        CheatsDB* cheatsDB = GetCheatInputSystem()->GetCheatsDB();
        rAssert( cheatsDB != NULL );

        for( unsigned int i = 0; i < NUM_CHEAT_DISPLAY_LINES; i++ )
        {
            char name[ 32 ];
            sprintf( name, "Cheat%d", i );
            Scrooby::Text* pText = m_cheatsPage->GetText( name );
            rAssert( pText != NULL );

            HeapMgr()->PushHeap( GMA_LEVEL_FE );

            if( cheatID < NUM_CHEATS )
            {
                char cheatInfo[ 64 ];

                const Cheat* cheat = cheatsDB->GetCheat( static_cast<eCheatID>( cheatID ) );
                if( cheat != NULL )
                {
                    CheatsDB::PrintCheatInfo( cheat, cheatInfo );

                    if( GetCheatInputSystem()->IsCheatEnabled( static_cast<eCheatID>( cheatID ) ) )
                    {
                        strcat( cheatInfo, "  (!)" ); // exclamation mark denotes cheat enabled
                    }
                }
                else
                {
                    // display un-registered cheat
                    //
                    sprintf( cheatInfo,
                             "Cheat ID [%02d]: *** Not Registered! ***", cheatID );
/*
                    // ignore/skip un-registered cheat and don't display anything
                    //
                    pText->SetString( 0, " " );
                    i--;
*/
                }

                pText->SetString( 0, cheatInfo );
            }
            else
            {
                pText->SetString( 0, " " );
            }

            HeapMgr()->PopHeap( GMA_LEVEL_FE );

            cheatID++;
        }
    }
}

void
CGuiScreenOptions::ProgressiveModeTestBegin()
{
    m_guiManager->DisplayMessage( CGuiScreenMessage::MSG_ID_PROGRESSIVE_SCAN_TEST, this );
    m_elapsedProgressiveModeTestTime = 0;

    rTunePrintf( "Testing progressive scan display ... ...\n" );

#ifdef RAD_PS2
    PS2Platform::GetInstance()->SetProgressiveMode( true );
#endif
}

void
CGuiScreenOptions::OnProgressiveModeTestEnd()
{
    m_guiManager->DisplayPrompt( PROMPT_DISPLAY_PROGRESSIVE_SCAN_CONFIRM, this );
    m_elapsedProgressiveModeTestTime = -1;

    rTunePrintf( "Progressive scan test completed.\n" );

#ifdef RAD_PS2
    PS2Platform::GetInstance()->SetProgressiveMode( false );
#endif
}

