//===========================================================================
// Copyright (C) 2000 Radical Entertainment Ltd.  All rights reserved.
//
// Component:   CGuiScreenOptions
//
// Description: 
//              
//
// Authors:     Tony Chu
//
// Revisions		Date			Author	    Revision
//                  2002/07/04      TChu        Created for SRR2
//
//===========================================================================

#ifndef GUISCREENOPTIONS_H
#define GUISCREENOPTIONS_H

//===========================================================================
// Nested Includes
//===========================================================================
#include <presentation/gui/guiscreen.h>
#include <cheats/cheatinputsystem.h>

//===========================================================================
// Forward References
//===========================================================================
class CGuiMenu;

//===========================================================================
// Interface Definitions
//===========================================================================
class CGuiScreenOptions : public CGuiScreen,
                          public ICheatEnteredCallback
{
public:
    CGuiScreenOptions( Scrooby::Screen* pScreen, CGuiEntity* pParent );
    virtual ~CGuiScreenOptions();

	virtual void HandleMessage( eGuiMessage message, 
			                    unsigned int param1 = 0,
								unsigned int param2 = 0 );

    virtual void OnCheatEntered( eCheatID cheatID, bool isEnabled );
    virtual CGuiMenu* HasMenu() { return m_pMenu; }

protected:
    void InitIntro();
	void InitRunning();
	void InitOutro();

private:
    void UpdateCheatsDisplay( int cheatID );

    void ProgressiveModeTestBegin();
    void OnProgressiveModeTestEnd();

    enum eMenuItem
    {
#ifdef RAD_PC
        MENU_ITEM_DISPLAY,
#endif
        MENU_ITEM_CONTROLLER,
        MENU_ITEM_SOUND,
        MENU_ITEM_VIEW_MOVIES,
        MENU_ITEM_VIEW_CREDITS,
        MENU_ITEM_DISPLAY_MODE,
#ifdef RAD_VITA
        MENU_ITEM_COMPILE_SHADERS,
#endif

        NUM_MENU_ITEMS
    };

    CGuiMenu* m_pMenu;

    Scrooby::Page* m_cheatsPage;
    Scrooby::Layer* m_cheatsOverlay;
    int m_startingCheatID;

    int m_elapsedProgressiveModeTestTime;

#ifdef RAD_VITA
    void UpdateFrameRateLabel();
    void ToggleFrameRate();

    Scrooby::Text* m_frameRateLabel;    // only when the page has no usable value text for the choice

    void UpdateShaderStatus();
    void UpdateShaderCompile();

    Scrooby::Text* m_compileLabel;      // texts added at runtime, styled like the other menu items
    Scrooby::Text* m_shaderStatus;
    int m_compileNext;                  // next program to compile, -1 when not compiling
    int m_compileDone;                  // programs compiled by this run
    bool m_compileLabelShown;           // the progress was on screen for a frame before the next (slow) step
    bool m_compileFinished;             // this run ended, keep showing its result
    int m_savedFrameRate;
#endif

};

#endif // GUISCREENOPTIONS_H
