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
        MENU_ITEM_MSAA,
        MENU_ITEM_FILTERING,
        MENU_ITEM_ANIMATION,
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
    void ToggleFrameRate( int direction );

    // The rows do not all fit on the screen: only SCROLL_VISIBLE_ROWS of them show, and the
    // group moves up by one row when the selection goes past the last visible one.
    void UpdateScroll( int selection );

    enum { SCROLL_VISIBLE_ROWS = 7, MAX_SCROLL_ROWS = 16 };

    void UpdateMsaaLabel();
    void ToggleMsaa( int direction );

    void UpdateFilteringLabel();
    void ToggleFiltering( int direction );

    void UpdateAnimationLabel();
    void ToggleAnimation( int direction );

    // the items that are changed with left/right and have no accept action
    bool IsValueItem( int item ) const
    {
        return item == MENU_ITEM_DISPLAY_MODE || item == MENU_ITEM_MSAA ||
               item == MENU_ITEM_FILTERING || item == MENU_ITEM_ANIMATION;
    }


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

    Scrooby::Group* m_menuGroup;
    Scrooby::Text* m_rowTexts[ MAX_SCROLL_ROWS ];   // row i = menu item i, the shader status row after the last
    int m_numRows;
    int m_rowSpacing;
    int m_baseShift;                    // the shift of the whole menu at the top of the list, in rows
    int m_scrollTop;                    // first visible row

    Scrooby::Text* m_msaaLabel;
    int m_savedMsaa;

    Scrooby::Text* m_filteringLabel;
    int m_savedFiltering;
    Scrooby::Text* m_animationLabel;
    int m_savedNpcDist;
#endif

};

#endif // GUISCREENOPTIONS_H