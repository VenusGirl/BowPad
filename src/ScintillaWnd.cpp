#include "stdafx.h"
#include "ScintillaWnd.h"
#include "XPMIcons.h"
#include "UnicodeUtils.h"

const int SC_MARGE_LINENUMBER = 0;
const int SC_MARGE_SYBOLE = 1;
const int SC_MARGE_FOLDER = 2;

const int MARK_BOOKMARK = 24;
const int MARK_HIDELINESBEGIN = 23;
const int MARK_HIDELINESEND = 22;

bool CScintillaWnd::Init(HINSTANCE hInst, HWND hParent)
{
    Scintilla_RegisterClasses(hInst);

    CreateEx(WS_EX_ACCEPTFILES, WS_CHILD | WS_VISIBLE, hParent, 0, L"Scintilla");

    if (!*this)
    {
        return false;
    }

    m_pSciMsg = (SciFnDirect)SendMessage(*this, SCI_GETDIRECTFUNCTION, 0, 0);
    m_pSciWndData = (sptr_t)SendMessage(*this, SCI_GETDIRECTPOINTER, 0, 0);

    if (m_pSciMsg==nullptr || m_pSciWndData==0)
        return false;

    Call(SCI_SETMARGINMASKN, SC_MARGE_FOLDER, SC_MASK_FOLDERS);
    Call(SCI_SETMARGINWIDTHN, SC_MARGE_FOLDER, 14);

    Call(SCI_SETMARGINMASKN, SC_MARGE_SYBOLE, (1<<MARK_BOOKMARK) | (1<<MARK_HIDELINESBEGIN) | (1<<MARK_HIDELINESEND));
    Call(SCI_MARKERSETALPHA, MARK_BOOKMARK, 70);
    Call(SCI_MARKERDEFINEPIXMAP, MARK_BOOKMARK, (LPARAM)bookmark_xpm);
    Call(SCI_MARKERDEFINEPIXMAP, MARK_HIDELINESBEGIN, (LPARAM)acTop_xpm);
    Call(SCI_MARKERDEFINEPIXMAP, MARK_HIDELINESEND, (LPARAM)acBottom_xpm);

    Call(SCI_SETMARGINSENSITIVEN, SC_MARGE_FOLDER, true);
    Call(SCI_SETMARGINSENSITIVEN, SC_MARGE_SYBOLE, true);

    Call(SCI_SETFOLDFLAGS, 16);
    Call(SCI_SETSCROLLWIDTHTRACKING, true);
    Call(SCI_SETSCROLLWIDTH, 1); // default empty document: override default width

    Call(SCI_SETWRAPVISUALFLAGS, SC_WRAPVISUALFLAG_MARGIN);
    Call(SCI_SETWRAPMODE, SC_WRAP_WORD);

    Call(SCI_STYLESETFORE, STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOWTEXT));
    Call(SCI_STYLESETBACK, STYLE_DEFAULT, ::GetSysColor(COLOR_WINDOW));
    Call(SCI_SETSELFORE, TRUE, ::GetSysColor(COLOR_HIGHLIGHTTEXT));
    Call(SCI_SETSELBACK, TRUE, ::GetSysColor(COLOR_HIGHLIGHT));
    Call(SCI_SETCARETFORE, ::GetSysColor(COLOR_WINDOWTEXT));
    Call(SCI_SETFONTQUALITY, SC_EFF_QUALITY_LCD_OPTIMIZED);

    Call(SCI_STYLESETVISIBLE, STYLE_CONTROLCHAR, TRUE);
    return true;
}


bool CScintillaWnd::InitScratch( HINSTANCE hInst )
{
    Scintilla_RegisterClasses(hInst);

    CreateEx(0, 0, NULL, 0, L"Scintilla");

    if (!*this)
    {
        return false;
    }

    m_pSciMsg = (SciFnDirect)SendMessage(*this, SCI_GETDIRECTFUNCTION, 0, 0);
    m_pSciWndData = (sptr_t)SendMessage(*this, SCI_GETDIRECTPOINTER, 0, 0);

    if (m_pSciMsg==nullptr || m_pSciWndData==0)
        return false;

    return true;
}

LRESULT CALLBACK CScintillaWnd::WinMsgHandler( HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam )
{
    if (prevWndProc)
        return CallWindowProc(prevWndProc, hwnd, uMsg, wParam, lParam);
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

void CScintillaWnd::UpdateLineNumberWidth()
{
    int linesVisible = (int) Call(SCI_LINESONSCREEN);
    if (linesVisible)
    {
        int firstVisibleLineVis = (int) Call(SCI_GETFIRSTVISIBLELINE);
        int lastVisibleLineVis = linesVisible + firstVisibleLineVis + 1;
        int i = 0;
        while (lastVisibleLineVis)
        {
            lastVisibleLineVis /= 10;
            i++;
        }
        i = max(i, 3);
        {
            int pixelWidth = int(8 + i * Call(SCI_TEXTWIDTH, STYLE_LINENUMBER, (LPARAM)"8"));
            Call(SCI_SETMARGINWIDTHN, SC_MARGE_LINENUMBER, pixelWidth);
        }
    }

}

void CScintillaWnd::SaveCurrentPos(CPosData * pPos)
{
    pPos->m_nFirstVisibleLine   = Call(SCI_GETFIRSTVISIBLELINE);
    pPos->m_nFirstVisibleLine   = Call(SCI_DOCLINEFROMVISIBLE, pPos->m_nFirstVisibleLine);

    pPos->m_nStartPos           = Call(SCI_GETANCHOR);
    pPos->m_nEndPos             = Call(SCI_GETCURRENTPOS);
    pPos->m_xOffset             = Call(SCI_GETXOFFSET);
    pPos->m_nSelMode            = Call(SCI_GETSELECTIONMODE);
    pPos->m_nScrollWidth        = Call(SCI_GETSCROLLWIDTH);
}

void CScintillaWnd::RestoreCurrentPos(CPosData pos)
{
    Call(SCI_GOTOPOS, 0);

    Call(SCI_SETSELECTIONMODE, pos.m_nSelMode);
    Call(SCI_SETANCHOR, pos.m_nStartPos);
    Call(SCI_SETCURRENTPOS, pos.m_nEndPos);
    Call(SCI_CANCEL);
    if (Call(SCI_GETWRAPMODE) != SC_WRAP_WORD)
    {
        // only offset if not wrapping, otherwise the offset isn't needed at all
        Call(SCI_SETSCROLLWIDTH, pos.m_nScrollWidth);
        Call(SCI_SETXOFFSET, pos.m_xOffset);
    }
    Call(SCI_CHOOSECARETX);

    size_t lineToShow = Call(SCI_VISIBLEFROMDOCLINE, pos.m_nFirstVisibleLine);
    Call(SCI_LINESCROLL, 0, lineToShow);
}

void CScintillaWnd::SetupLexerForExt( const std::wstring& ext )
{
    auto lexerdata = CLexStyles::Instance().GetLexerDataForExt(CUnicodeUtils::StdGetUTF8(ext));
    auto langdata = CLexStyles::Instance().GetKeywordsForExt(CUnicodeUtils::StdGetUTF8(ext));
    SetupLexer(lexerdata, langdata);
}

void CScintillaWnd::SetupLexerForLang( const std::wstring& lang )
{
    auto lexerdata = CLexStyles::Instance().GetLexerDataForLang(CUnicodeUtils::StdGetUTF8(lang));
    auto langdata = CLexStyles::Instance().GetKeywordsForLang(CUnicodeUtils::StdGetUTF8(lang));
    SetupLexer(lexerdata, langdata);
}

void CScintillaWnd::SetupLexer( const LexerData& lexerdata, const std::map<int, std::string>& langdata )
{
    SetupDefaultStyles();
    Call(SCI_STYLECLEARALL);

    Call(SCI_SETLEXER, lexerdata.ID);

    for (auto it: lexerdata.Properties)
    {
        Call(SCI_SETPROPERTY, (WPARAM)it.first.c_str(), (LPARAM)it.second.c_str());
    }
    for (auto it: lexerdata.Styles)
    {
        Call(SCI_STYLESETFORE, it.first, it.second.ForegroundColor);
        Call(SCI_STYLESETBACK, it.first, it.second.BackgroundColor);
        if (!it.second.FontName.empty())
            Call(SCI_STYLESETFONT, it.first, (LPARAM)it.second.FontName.c_str());
        switch (it.second.FontStyle)
        {
        case FONTSTYLE_NORMAL:
            break;  // do nothing
        case FONTSTYLE_BOLD:
            Call(SCI_STYLESETBOLD, it.first, 1);
            break;
        case FONTSTYLE_ITALIC:
            Call(SCI_STYLESETITALIC, it.first, 1);
            break;
        case FONTSTYLE_UNDERLINED:
            Call(SCI_STYLESETUNDERLINE, it.first, 1);
            break;
        }
        if (it.second.FontSize)
            Call(SCI_STYLESETSIZE, it.first, it.second.FontSize);
    }
    for (auto it: langdata)
    {
        Call(SCI_SETKEYWORDS, it.first, (LPARAM)it.second.c_str());
    }
}

void CScintillaWnd::SetupDefaultStyles()
{
    Call(SCI_STYLERESETDEFAULT);
    // if possible, use the Consolas font
    // to determine whether Consolas is available, try to create
    // a font with it.
    HFONT hFont = CreateFont(0, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY, FIXED_PITCH, L"Consolas");
    if (hFont)
    {
        Call(SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Consolas");
        DeleteObject(hFont);
    }
    else
        Call(SCI_STYLESETFONT, STYLE_DEFAULT, (LPARAM)"Courier New");
    Call(SCI_STYLESETSIZE, STYLE_DEFAULT, 10);
}
