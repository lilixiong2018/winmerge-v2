/////////////////////////////////////////////////////////////////////////////
//
//    WinMerge: An interactive diff/merge utility
//    Copyright (C) 1997 Dean P. Grimm
//
//    This program is free software; you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation; either version 2 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program; if not, write to the Free Software
//    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
//
/////////////////////////////////////////////////////////////////////////////

/** 
 * @file  LocationView.cpp
 *
 * @brief Implementation file for CLocationView
 *
 */


#include "StdAfx.h"
#include "LocationView.h"
#include <vector>
#include "Merge.h"
#include "OptionsMgr.h"
#include "MergeEditView.h"
#include "MergeDoc.h"
#include "BCMenu.h"
#include "OptionsDef.h"
#include "Bitmap.h"
#include "memdc.h"
#include "SyntaxColors.h"

#ifdef _DEBUG
#define new DEBUG_NEW
#endif

using std::vector;

/** @brief Size of empty frame above and below bars (in pixels). */
static const int Y_OFFSET = 5;
/** @brief Size of y-margin for visible area indicator (in pixels). */
static const long INDICATOR_MARGIN = 2;
/** @brief Max pixels in view per line in file. */
static const double MAX_LINEPIX = 4.0;
/** @brief Top of difference marker, relative to difference start. */
static const int DIFFMARKER_TOP = 3;
/** @brief Bottom of difference marker, relative to difference start. */
static const int DIFFMARKER_BOTTOM = 3;
/** @brief Width of difference marker. */
static const int DIFFMARKER_WIDTH = 6;
/** @brief Minimum height of the visible area indicator */
static const int INDICATOR_MIN_HEIGHT = 2;

/** 
 * @brief Bars in location pane
 */
enum LOCBAR_TYPE
{
	BAR_NONE = -1,	/**< No bar in given coords */
	BAR_0,			/**< first bar in given coords */
	BAR_1,			/**< second bar in given coords */
	BAR_2,			/**< third side bar in given coords */
	BAR_YAREA,		/**< Y-Coord in bar area */
};

const COLORREF clrBackground = RGB(0xe4, 0xe4, 0xf4);

/////////////////////////////////////////////////////////////////////////////
// CLocationView

IMPLEMENT_DYNCREATE(CLocationView, CView)


CLocationView::CLocationView()
	: m_visibleTop(-1)
	, m_visibleBottom(-1)
//	MOVEDLINE_LIST m_movedLines; //*< List of moved block connecting lines */
	, m_hwndFrame(nullptr)
	, m_pSavedBackgroundBitmap(nullptr)
	, m_bDrawn(false)
	, m_bRecalculateBlocks(TRUE) // calculate for the first time
{
	// NB: set m_bIgnoreTrivials to false to see trivial diffs in the LocationView
	// There is no GUI to do this

	SetConnectMovedBlocks(GetOptionsMgr()->GetInt(OPT_CONNECT_MOVED_BLOCKS));

	std::fill_n(m_nSubLineCount, countof(m_nSubLineCount), 0);
}

CLocationView::~CLocationView()
{
}

BEGIN_MESSAGE_MAP(CLocationView, CView)
	//{{AFX_MSG_MAP(CLocationView)
	ON_WM_LBUTTONDOWN()
	ON_WM_LBUTTONUP()
	ON_WM_MOUSEMOVE()
	ON_WM_MOUSEACTIVATE()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_CONTEXTMENU()
	ON_WM_CLOSE()
	ON_WM_SIZE()
	ON_WM_VSCROLL()
	ON_WM_ERASEBKGND()
	//}}AFX_MSG_MAP
END_MESSAGE_MAP()


void CLocationView::SetConnectMovedBlocks(int displayMovedBlocks) 
{
	if (m_displayMovedBlocks == displayMovedBlocks)
		return;

	GetOptionsMgr()->SaveOption(OPT_CONNECT_MOVED_BLOCKS, displayMovedBlocks);
	m_displayMovedBlocks = displayMovedBlocks;
	if (this->GetSafeHwnd() != NULL)
		if (IsWindowVisible())
			Invalidate();
}

/////////////////////////////////////////////////////////////////////////////
// CLocationView diagnostics
#ifdef _DEBUG
CMergeDoc* CLocationView::GetDocument() // non-debug version is inline
{
	ASSERT(m_pDocument->IsKindOf(RUNTIME_CLASS(CMergeDoc)));
	return (CMergeDoc*)m_pDocument;
}
#endif //_DEBUG

/////////////////////////////////////////////////////////////////////////////
// CLocationView message handlers

/**
 * @brief Force recalculation and update of location pane.
 * This method forces location pane to first recalculate its data and
 * then repaint itself. This method bypasses location pane's caching
 * of the diff data.
 */
void CLocationView::ForceRecalculate()
{
	m_bRecalculateBlocks = TRUE;
	Invalidate();
}

/** 
 * @brief Update view.
 */
void CLocationView::OnUpdate( CView* pSender, LPARAM lHint, CObject* pHint )
{
	UNREFERENCED_PARAMETER(pSender);
	UNREFERENCED_PARAMETER(lHint);

	m_bRecalculateBlocks = TRUE;
	Invalidate();
}

/** 
 * @brief Override for CMemDC to work.
 */
BOOL CLocationView::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

/**
 * @brief Draw custom (non-white) background.
 * @param [in] pDC Pointer to draw context.
 */
void CLocationView::DrawBackground(CDC* pDC)
{
	// Set brush to desired background color
	CBrush backBrush(clrBackground);
	
	// Save old brush
	CBrush* pOldBrush = pDC->SelectObject(&backBrush);
	
	CRect rect;
	pDC->GetClipBox(&rect);     // Erase the area needed
	
	pDC->PatBlt(rect.left, rect.top, rect.Width(), rect.Height(), PATCOPY);

	pDC->SelectObject(pOldBrush);
}

/**
 * @brief Calculate bar coordinates and scaling factors.
 */
void CLocationView::CalculateBars()
{
	CMergeDoc *pDoc = GetDocument();
	CRect rc;
	GetClientRect(rc);
	const int w = rc.Width() / (pDoc->m_nBuffers * 2);
	const int margin = (rc.Width() - w * pDoc->m_nBuffers) / (pDoc->m_nBuffers + 1);
	int pane;
	for (pane = 0; pane < pDoc->m_nBuffers; pane++)
	{
		m_bar[pane].left = pane * (w + margin) + margin;
		m_bar[pane].right = m_bar[pane].left + w;
	}	const double hTotal = rc.Height() - (2 * Y_OFFSET); // Height of draw area
	int nbLines = 0;
	pDoc->ForEachActiveGroupView([&](auto& pView) {
		nbLines = max(nbLines, pView->GetSubLineCount());
	});

	m_lineInPix = hTotal / nbLines;
	m_pixInLines = nbLines / hTotal;
	if (m_lineInPix > MAX_LINEPIX)
	{
		m_lineInPix = MAX_LINEPIX;
		m_pixInLines = 1 / MAX_LINEPIX;
	}

	for (pane = 0; pane < pDoc->m_nBuffers; pane++)
	{
		m_bar[pane].top = Y_OFFSET - 1;
		m_bar[pane].bottom = (LONG)(m_lineInPix * nbLines + Y_OFFSET + 1);
	}
}

/**
 * @brief Calculate difference lines and coordinates.
 * This function calculates begin- and end-lines of differences when word-wrap
 * is enabled. Otherwise the value from original difflist is used. Line
 * numbers are also converted to coordinates in the window. All calculated
 * (and not ignored) differences are added to the new list.
 */
void CLocationView::CalculateBlocks()
{
	// lineposition in pixels.
	int nBeginY;
	int nEndY;

	m_diffBlocks.clear();

	CMergeDoc *pDoc = GetDocument();
	const int nDiffs = pDoc->m_diffList.GetSize();
	if (nDiffs > 0)
		m_diffBlocks.reserve(nDiffs); // Pre-allocate space for the list.

	int nGroup = pDoc->GetActiveMergeView()->m_nThisGroup;
	int nLineCount = pDoc->GetView(nGroup, 0)->GetLineCount();
	int nDiff = pDoc->m_diffList.FirstSignificantDiff();
	while (nDiff != -1)
	{
		DIFFRANGE diff;
		VERIFY(pDoc->m_diffList.GetDiff(nDiff, diff));

		CMergeEditView *pView = pDoc->GetView(nGroup, 0);

		DiffBlock block;
		int i, nBlocks = 0;
		int bs[4] = {0};
		int minY = INT_MAX, maxY = -1;

		bs[nBlocks++] = diff.dbegin;
		for (i = 0; i < pDoc->m_nBuffers; i++)
		{
			if (diff.blank[i] >= 0)
			{
				if (minY > diff.blank[i])
					minY = diff.blank[i];
				if (maxY < diff.blank[i])
					maxY = diff.blank[i];
			}
		}
		if (minY == maxY)
		{
			bs[nBlocks++] = minY;
		}
		else if (maxY != -1)
		{
			bs[nBlocks++] = minY;
			bs[nBlocks++] = maxY;
		}
		bs[nBlocks] = diff.dend + 1;
		if (bs[nBlocks] >= nLineCount)
			bs[nBlocks] = nLineCount - 1;

		for (i = 0; i < nBlocks; i++)
		{
			CalculateBlocksPixel(
				pView->GetSubLineIndex(bs[i]),
				pView->GetSubLineIndex(bs[i + 1]),
				pView->GetSubLines(bs[i + 1]), nBeginY, nEndY);

			block.top_line = bs[i];
			block.bottom_line = bs[i + 1];
			block.top_coord = nBeginY;
			block.bottom_coord = nEndY;
			block.diff_index = nDiff;
			block.op = diff.op;
			m_diffBlocks.push_back(block);
		}

		nDiff = pDoc->m_diffList.NextSignificantDiff(nDiff);
	}
	m_bRecalculateBlocks = FALSE;
}

/**
 * @brief Calculate Blocksize to pixel.
 * @param [in] nBlockStart line where block starts
 * @param [in] nBlockEnd   line where block ends 
 * @param [in] nBlockLength length of the block
 * @param [in,out] nBeginY pixel in y  where block starts
 * @param [in,out] nEndY   pixel in y  where block ends

 */
void CLocationView::CalculateBlocksPixel(int nBlockStart, int nBlockEnd,
		int nBlockLength, int &nBeginY, int &nEndY)
{
	// Count how many line does the diff block have.
	const int nBlockHeight = nBlockEnd - nBlockStart + nBlockLength;

	// Convert diff block size from lines to pixels.
	nBeginY = (int)(nBlockStart * m_lineInPix + Y_OFFSET);
	nEndY = (int)((nBlockStart + nBlockHeight) * m_lineInPix + Y_OFFSET);
}

static COLORREF GetIntermediateColor(COLORREF a, COLORREF b)
{
	const int R = (GetRValue(a) - GetRValue(b)) / 2 + GetRValue(b);
	const int G = (GetGValue(a) - GetGValue(b)) / 2 + GetGValue(b);
	const int B = (GetBValue(a) - GetBValue(b)) / 2 + GetBValue(b);
	return RGB(R, G, B);
}

static COLORREF GetDarkenColor(COLORREF a, double l)
{
	const int R = static_cast<int>(GetRValue(a) * l);
	const int G = static_cast<int>(GetGValue(a) * l);
	const int B = static_cast<int>(GetBValue(a) * l);
	return RGB(R, G, B);
}

/** 
 * @brief Draw maps of files.
 *
 * Draws maps of differences in files. Difference list is walked and
 * every difference is drawn with same colors as in editview.
 * @note We MUST use doubles when calculating coords to avoid rounding
 * to integers. Rounding causes miscalculation of coords.
 * @sa CLocationView::DrawRect()
 */
void CLocationView::OnDraw(CDC* pDC)
{
	CMergeDoc *pDoc = GetDocument();
	int nGroup = pDoc->GetActiveMergeView()->m_nThisGroup;
	if (pDoc->GetView(nGroup, 0) == nullptr)
		return;

	if (!pDoc->GetView(nGroup, 0)->IsInitialized()) return;

	BOOL bEditedAfterRescan = FALSE;
	int nPaneNotModified = -1;
	for (int pane = 0; pane < pDoc->m_nBuffers; pane++)
	{
		if (!pDoc->IsEditedAfterRescan(pane))
			nPaneNotModified = pane;
		else
			bEditedAfterRescan = TRUE;
	}

	CRect rc;
	GetClientRect(&rc);

	CMyMemDC dc(pDC, &rc);

	COLORREF cr[3] = {CLR_NONE, CLR_NONE, CLR_NONE};
	COLORREF crt = CLR_NONE; // Text color
	bool bwh = false;

	m_movedLines.RemoveAll();

	CalculateBars();
	DrawBackground(&dc);

	COLORREF clrFace    = clrBackground;
	COLORREF clrShadow  = GetSysColor(COLOR_BTNSHADOW);
	COLORREF clrShadow2 = GetIntermediateColor(clrFace, clrShadow);
	COLORREF clrShadow3 = GetIntermediateColor(clrFace, clrShadow2);
	COLORREF clrShadow4 = GetIntermediateColor(clrFace, clrShadow3);

	// Draw bar outlines
	CPen* oldObj = (CPen*)dc.SelectStockObject(NULL_PEN);
	CBrush brush(pDoc->GetView(nGroup, 0)->GetColor(COLORINDEX_WHITESPACE));
	CBrush* oldBrush = (CBrush*)dc.SelectObject(&brush);
	for (int pane = 0; pane < pDoc->m_nBuffers; pane++)
	{
		int nBottom = (int)(m_lineInPix * pDoc->GetView(nGroup, pane)->GetSubLineCount() + Y_OFFSET + 1);
		CBrush *pOldBrush = NULL;
		if (pDoc->IsEditedAfterRescan(pane))
			pOldBrush = (CBrush *)dc.SelectStockObject(HOLLOW_BRUSH);
		dc.Rectangle(m_bar[pane]);
		if (pOldBrush)
			dc.SelectObject(pOldBrush);

		CRect rect = m_bar[pane];
		rect.InflateRect(1, 1);
		dc.Draw3dRect(rect, clrShadow4, clrShadow3);
		rect.InflateRect(-1, -1);
		dc.Draw3dRect(rect, clrShadow2, clrShadow);
	}
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldBrush);
	dc.SelectObject(oldObj);

	// Iterate the differences list and draw differences as colored blocks.

	// Don't recalculate blocks if we earlier determined it is not needed
	// This may save lots of processing
	if (m_bRecalculateBlocks)
		CalculateBlocks();

	int nPrevEndY = -1;
	const int nCurDiff = pDoc->GetCurrentDiff();

	vector<DiffBlock>::const_iterator iter = m_diffBlocks.begin();
	for (; iter != m_diffBlocks.end(); ++iter)
	{
		if (nPaneNotModified == -1)
			continue;
		CMergeEditView *pView = pDoc->GetView(nGroup, nPaneNotModified);
		const BOOL bInsideDiff = (nCurDiff == (*iter).diff_index);

		if ((nPrevEndY != (*iter).bottom_coord) || bInsideDiff)
		{
			for (int pane = 0; pane < pDoc->m_nBuffers; pane++)
			{
				if (pDoc->IsEditedAfterRescan(pane))
					continue;
				// Draw 3way-diff state
				if (pDoc->m_nBuffers == 3 && pane < 2)
				{
					CRect r(m_bar[pane].right - 1, (*iter).top_coord, m_bar[pane + 1].left + 1, (*iter).bottom_coord);
					if ((pane == 0 && (*iter).op == OP_3RDONLY) || (pane == 1 && (*iter).op == OP_1STONLY))
						DrawRect(&dc, r, RGB(255, 255, 127), false);
					else if ((*iter).op == OP_2NDONLY)
						DrawRect(&dc, r, RGB(127, 255, 255), false);
					else if ((*iter).op == OP_DIFF)
						DrawRect(&dc, r, RGB(255, 0, 0), false);
				}
				// Draw block
				pDoc->GetView(0, pane)->GetLineColors2((*iter).top_line, 0, cr[pane], crt, bwh);
				CRect r(m_bar[pane].left, (*iter).top_coord, m_bar[pane].right, (*iter).bottom_coord);
				DrawRect(&dc, r, cr[pane], bInsideDiff);
			}
		}
		nPrevEndY = (*iter).bottom_coord;

		// Test if we draw a connector
		BOOL bDisplayConnectorFromLeft = FALSE;
		BOOL bDisplayConnectorFromRight = FALSE;

		switch (m_displayMovedBlocks)
		{
		case DISPLAY_MOVED_FOLLOW_DIFF:
			// display moved block only for current diff
			if (!bInsideDiff)
				break;
			// two sides may be linked to a block somewhere else
			bDisplayConnectorFromLeft = TRUE;
			bDisplayConnectorFromRight = TRUE;
			break;
		case DISPLAY_MOVED_ALL:
			// we display all moved blocks, so once direction is enough
			bDisplayConnectorFromLeft = TRUE;
			break;
		default:
			break;
		}

		if (bEditedAfterRescan)
			continue;

		for (int pane = 0; pane < pDoc->m_nBuffers; pane++)
		{
			if (bDisplayConnectorFromLeft && pane < 2)
			{
				int apparent0 = (*iter).top_line;
				int apparent1 = pDoc->RightLineInMovedBlock(pane, apparent0);
				const int nBlockHeight = (*iter).bottom_line - (*iter).top_line;
				if (apparent1 != -1)
				{
					MovedLine line;
					CPoint start;
					CPoint end;

					apparent0 = pView->GetSubLineIndex(apparent0);
					apparent1 = pView->GetSubLineIndex(apparent1);

					start.x = m_bar[pane].right;
					int leftUpper = (int) (apparent0 * m_lineInPix + Y_OFFSET);
					int leftLower = (int) ((nBlockHeight + apparent0) * m_lineInPix + Y_OFFSET);
					start.y = leftUpper + (leftLower - leftUpper) / 2;
					end.x = m_bar[pane + 1].left;
					int rightUpper = (int) (apparent1 * m_lineInPix + Y_OFFSET);
					int rightLower = (int) ((nBlockHeight + apparent1) * m_lineInPix + Y_OFFSET);
					end.y = rightUpper + (rightLower - rightUpper) / 2;
					line.ptLeft = start;
					line.ptRight = end;
					m_movedLines.AddTail(line);
				}
			}

			if (bDisplayConnectorFromRight && pane > 0)
			{
				int apparent1 = (*iter).top_line;
				int apparent0 = pDoc->LeftLineInMovedBlock(pane, apparent1);
				const int nBlockHeight = (*iter).bottom_line - (*iter).top_line;
				if (apparent0 != -1)
				{
					MovedLine line;
					CPoint start;
					CPoint end;

					apparent0 = pView->GetSubLineIndex(apparent0);
					apparent1 = pView->GetSubLineIndex(apparent1);

					start.x = m_bar[pane - 1].right;
					int leftUpper = (int) (apparent0 * m_lineInPix + Y_OFFSET);
					int leftLower = (int) ((nBlockHeight + apparent0) * m_lineInPix + Y_OFFSET);
					start.y = leftUpper + (leftLower - leftUpper) / 2;
					end.x = m_bar[pane].left;
					int rightUpper = (int) (apparent1 * m_lineInPix + Y_OFFSET);
					int rightLower = (int) ((nBlockHeight + apparent1) * m_lineInPix + Y_OFFSET);
					end.y = rightUpper + (rightLower - rightUpper) / 2;
					line.ptLeft = start;
					line.ptRight = end;
					m_movedLines.AddTail(line);
				}
			}
		}
	}

	if (m_displayMovedBlocks != DISPLAY_MOVED_NONE)
		DrawConnectLines(&dc);

	m_pSavedBackgroundBitmap.reset(CopyRectToBitmap(&dc, rc));

	// Since we have invalidated locationbar there is no previous
	// arearect to remove
	m_visibleTop = -1;
	m_visibleBottom = -1;
	DrawVisibleAreaRect(&dc);

	m_bDrawn = true;
}

/** 
 * @brief Draw one block of map.
 * @param [in] pDC Draw context.
 * @param [in] r Rectangle to draw.
 * @param [in] cr Color for rectangle.
 * @param [in] bSelected Is rectangle for selected difference?
 */
void CLocationView::DrawRect(CDC* pDC, const CRect& r, COLORREF cr, BOOL bSelected)
{
	// Draw only colored blocks
	if (cr != CLR_NONE && cr != GetSysColor(COLOR_WINDOW))
	{
		CBrush brush(cr);
		CRect drawRect(r);
		drawRect.DeflateRect(1, 0);

		// With long files and small difference areas rect may become 0-height.
		// Make sure all diffs are drawn at least one pixel height.
		if (drawRect.Height() < 1)
			++drawRect.bottom;
		pDC->FillSolidRect(drawRect, cr);
		CRect drawRect2(drawRect.left, drawRect.top, drawRect.right, drawRect.top + 1);
		pDC->FillSolidRect(drawRect2, GetDarkenColor(cr, 0.96));
		CRect drawRect3(drawRect.left, drawRect.bottom - 1, drawRect.right, drawRect.bottom);
		pDC->FillSolidRect(drawRect3, GetDarkenColor(cr, 0.91));

		if (bSelected)
		{
			DrawDiffMarker(pDC, r.top);
		}
	}
}

/**
 * @brief Capture the mouse target.
 */
void CLocationView::OnLButtonDown(UINT nFlags, CPoint point) 
{
	SetCapture();

	if (!GotoLocation(point, false))
		CView::OnLButtonDown(nFlags, point);
}

/**
 * @brief Release the mouse target.
 */
void CLocationView::OnLButtonUp(UINT nFlags, CPoint point) 
{
	ReleaseCapture();

	CView::OnLButtonUp(nFlags, point);
}

/**
 * @brief Process drag action on a captured mouse.
 *
 * Reposition on every dragged movement.
 * The Screen update stress will be similar to a mouse wheeling.:-)
 */
void CLocationView::OnMouseMove(UINT nFlags, CPoint point) 
{
	if (GetCapture() == this)
	{
		CMergeDoc *pDoc = GetDocument();
		int nGroup = pDoc->GetActiveMergeView()->m_nThisGroup;

		// Don't go above bars.
		point.y = max(point.y, Y_OFFSET);

		// Vertical scroll handlers are range safe, so there is no need to
		// make sure value is valid and in range.
		int nSubLine = (int) (m_pixInLines * (point.y - Y_OFFSET));
		nSubLine -= pDoc->GetView(nGroup, 0)->GetScreenLines() / 2;
		if (nSubLine < 0)
			nSubLine = 0;

		// Just a random choose as both view share the same scroll bar.
		CWnd *pView = pDoc->GetView(nGroup, 0);

		SCROLLINFO si = { sizeof SCROLLINFO };
		si.fMask = SIF_POS;
		si.nPos = nSubLine;
		pView->SetScrollInfo(SB_VERT, &si);

		// The views are child windows of a splitter windows. Splitter window
		// doesn't accept scroll bar updates not send from scroll bar control.
		// So we need to update both views.
		int pane;
		int nBuffers = pDoc->m_nBuffers;
		for (pane = 0; pane < nBuffers; pane++)
			pDoc->GetView(nGroup, pane)->SendMessage(WM_VSCROLL, MAKEWPARAM(SB_THUMBPOSITION, 0), NULL);
	}

	CView::OnMouseMove(nFlags, point);
}

int  CLocationView::OnMouseActivate(CWnd* pDesktopWnd, UINT nHitTest, UINT message)
{
	return MA_NOACTIVATE;
}

/**
 * User left double-clicked mouse
 * @todo We can give alternative action to a double clicking. 
 */
void CLocationView::OnLButtonDblClk(UINT nFlags, CPoint point) 
{
	if (!GotoLocation(point, false))
		CView::OnLButtonDblClk(nFlags, point);
}

/**
 * @brief Scroll both views to point given.
 *
 * Scroll views to given line. There is two ways to scroll, based on
 * view lines (ghost lines counted in) or on real lines (no ghost lines).
 * In most cases view lines should be used as it avoids real line number
 * calculation and is able to scroll to all lines - real line numbers
 * cannot be used to scroll to ghost lines.
 *
 * @param [in] point Point to move to
 * @param [in] bRealLine TRUE if we want to scroll using real line num,
 * FALSE if view linenumbers are OK.
 * @return TRUE if succeeds, FALSE if point not inside bars.
 */
bool CLocationView::GotoLocation(const CPoint& point, bool bRealLine)
{
	CRect rc;
	GetClientRect(rc);
	CMergeDoc* pDoc = GetDocument();

	if (!pDoc->GetActiveMergeView())
		return false;

	int line = -1;
	int bar = IsInsideBar(rc, point);
	if (bar == BAR_0 || bar == BAR_1 || bar == BAR_2)
	{
		line = GetLineFromYPos(point.y, bar, bRealLine);
	}
	else if (bar == BAR_YAREA)
	{
		// Outside bars, use left bar
		bar = BAR_0;
		line = GetLineFromYPos(point.y, bar, FALSE);
	}
	else
		return false;

	pDoc->GetActiveMergeGroupView(0)->GotoLine(line, bRealLine, bar);
	if (bar == BAR_0 || bar == BAR_1 || bar == BAR_2)
		pDoc->GetActiveMergeGroupView(bar)->SetFocus();

	return true;
}

/**
 * @brief Handle scroll events sent directly.
 *
 */
void CLocationView::OnVScroll(UINT nSBCode, UINT nPos, CScrollBar *pScrollBar)
{
	if (pScrollBar == NULL)
	{
		// Scroll did not come frome a scroll bar
		// Send it to the right view instead
 	  CMergeDoc *pDoc = GetDocument();
		pDoc->GetActiveMergeGroupView(pDoc->m_nBuffers - 1)->SendMessage(WM_VSCROLL,
			MAKELONG(nSBCode, nPos), (LPARAM)NULL);
		return;
	}
	CView::OnVScroll (nSBCode, nPos, pScrollBar);
}

/**
 * Show context menu and handle user selection.
 */
void CLocationView::OnContextMenu(CWnd* pWnd, CPoint point) 
{
	if (point.x == -1 && point.y == -1)
	{
		//keystroke invocation
		CRect rect;
		GetClientRect(rect);
		ClientToScreen(rect);

		point = rect.TopLeft();
		point.Offset(5, 5);
	}

	CRect rc;
	CPoint pt = point;
	GetClientRect(rc);
	ScreenToClient(&pt);
	BCMenu menu;
	VERIFY(menu.LoadMenu(IDR_POPUP_LOCATIONBAR));
	theApp.TranslateMenu(menu.m_hMenu);

	BCMenu* pPopup = static_cast<BCMenu *>(menu.GetSubMenu(0));
	ASSERT(pPopup != NULL);

	CCmdUI cmdUI;
	cmdUI.m_pMenu = pPopup;
	cmdUI.m_nIndexMax = cmdUI.m_pMenu->GetMenuItemCount();
	for (cmdUI.m_nIndex = 0 ; cmdUI.m_nIndex < cmdUI.m_nIndexMax ; ++cmdUI.m_nIndex)
	{
		cmdUI.m_nID = cmdUI.m_pMenu->GetMenuItemID(cmdUI.m_nIndex);
		switch (cmdUI.m_nID)
		{
		case ID_DISPLAY_MOVED_NONE:
			cmdUI.SetRadio(m_displayMovedBlocks == DISPLAY_MOVED_NONE);
			break;
		case ID_DISPLAY_MOVED_ALL:
			cmdUI.SetRadio(m_displayMovedBlocks == DISPLAY_MOVED_ALL);
			break;
		case ID_DISPLAY_MOVED_FOLLOW_DIFF:
			cmdUI.SetRadio(m_displayMovedBlocks == DISPLAY_MOVED_FOLLOW_DIFF);
			break;
		}
	}

	String strItem;
	String strNum;
	int nLine = -1;
	int bar = IsInsideBar(rc, pt);

	// If cursor over bar, format string with linenumber, else disable item
	if (bar != BAR_NONE)
	{
		// If outside bar area use left bar
		if (bar == BAR_YAREA)
			bar = BAR_0;
		nLine = GetLineFromYPos(pt.y, bar);
		strNum = strutils::to_str(nLine + 1); // Show linenumber not lineindex
	}
	else
		pPopup->EnableMenuItem(ID_LOCBAR_GOTODIFF, MF_GRAYED);
	strItem = strutils::format_string1(_("G&oto Line %1"), strNum);
	pPopup->SetMenuText(ID_LOCBAR_GOTODIFF, strItem.c_str(), MF_BYCOMMAND);

	// invoke context menu
	// we don't want to use the main application handlers, so we use flags TPM_NONOTIFY | TPM_RETURNCMD
	// and handle the command after TrackPopupMenu
	int command = pPopup->TrackPopupMenu(TPM_LEFTALIGN | TPM_RIGHTBUTTON | TPM_NONOTIFY  | TPM_RETURNCMD, point.x, point.y, AfxGetMainWnd());

	CMergeDoc* pDoc = GetDocument();
	switch (command)
	{
	case ID_LOCBAR_GOTODIFF:
		pDoc->GetActiveMergeGroupView(0)->GotoLine(nLine, true, bar);
		if (bar == BAR_0 || bar == BAR_1 || bar == BAR_2)
			pDoc->GetActiveMergeGroupView(bar)->SetFocus();
		break;
	case ID_EDIT_WMGOTO:
		pDoc->GetActiveMergeGroupView(0)->WMGoto();
		break;
	case ID_DISPLAY_MOVED_NONE:
		SetConnectMovedBlocks(DISPLAY_MOVED_NONE);
		pDoc->SetDetectMovedBlocks(FALSE);
		break;
	case ID_DISPLAY_MOVED_ALL:
		SetConnectMovedBlocks(DISPLAY_MOVED_ALL);
		pDoc->SetDetectMovedBlocks(TRUE);
		break;
	case ID_DISPLAY_MOVED_FOLLOW_DIFF:
		SetConnectMovedBlocks(DISPLAY_MOVED_FOLLOW_DIFF);
		pDoc->SetDetectMovedBlocks(TRUE);
		break;
	}
}

/** 
 * @brief Calculates view/real line in file from given YCoord in bar.
 * @param [in] nYCoord ycoord in pane
 * @param [in] bar bar/file
 * @param [in] bRealLine TRUE if real line is returned, FALSE for view line
 * @return 0-based index of view/real line in file [0...lines-1]
 */
int CLocationView::GetLineFromYPos(int nYCoord, int bar, BOOL bRealLine)
{
	CMergeEditView *pView = GetDocument()->GetActiveMergeGroupView(bar);

	int nSubLineIndex = (int) (m_pixInLines * (nYCoord - Y_OFFSET));

	// Keep sub-line index in range.
	if (nSubLineIndex < 0)
	{
		nSubLineIndex = 0;
	}
	else if (nSubLineIndex >= pView->GetSubLineCount())
	{
		nSubLineIndex = pView->GetSubLineCount() - 1;
	}

	// Find the real (not wrapped) line number from sub-line index.
	int nLine = 0;
	int nSubLine = 0;
	pView->GetLineBySubLine(nSubLineIndex, nLine, nSubLine);

	// Convert line number to line index.
	if (nLine > 0)
	{
		nLine -= 1;
	}

	// We've got a view line now
	if (bRealLine == FALSE)
		return nLine;

	// Get real line (exclude ghost lines)
	CMergeDoc* pDoc = GetDocument();
	const int nRealLine = pDoc->m_ptBuf[bar]->ComputeRealLine(nLine);
	return nRealLine;
}

/** 
 * @brief Determines if given coords are inside left/right bar.
 * @param rc [in] size of locationpane client area
 * @param pt [in] point we want to check, in client coordinates.
 * @return LOCBAR_TYPE area where point is.
 */
int CLocationView::IsInsideBar(const CRect& rc, const POINT& pt)
{
	int retVal = BAR_NONE;
	CMergeDoc *pDoc = GetDocument();
	for (int pane = 0; pane < pDoc->m_nBuffers; pane++)
	{
		if (m_bar[pane].PtInRect(pt))
		{
			retVal = BAR_0 + pane;
			break;
		}
	}
	return retVal;
}

/** 
 * @brief Draws rect indicating visible area in file views.
 *
 * @param [in] nTopLine New topline for indicator
 * @param [in] nBottomLine New bottomline for indicator
 * @todo This function dublicates too much DrawRect() code.
 */
void CLocationView::DrawVisibleAreaRect(CDC *pClientDC, int nTopLine, int nBottomLine)
{
	CMergeDoc* pDoc = GetDocument();
	int nGroup = pDoc->GetActiveMergeView()->m_nThisGroup;
	
	if (nTopLine == -1)
		nTopLine = pDoc->GetView(nGroup, 0)->GetTopSubLine();
	
	if (nBottomLine == -1)
	{
		const int nScreenLines = pDoc->GetView(nGroup, 1)->GetScreenLines();
		nBottomLine = nTopLine + nScreenLines;
	}

	CRect rc;
	GetClientRect(rc);
	int nbLines = INT_MAX;
	pDoc->ForEachActiveGroupView([&](auto& pView) {
		nbLines = min(nbLines, pView->GetSubLineCount());
	});

	int nTopCoord = static_cast<int>(Y_OFFSET +
			(static_cast<double>(nTopLine * m_lineInPix)));
	int nBottomCoord = static_cast<int>(Y_OFFSET +
			(static_cast<double>(nBottomLine * m_lineInPix)));
	
	double xbarBottom = min(nbLines / m_pixInLines + Y_OFFSET, rc.Height() - Y_OFFSET);
	int barBottom = (int)xbarBottom;
	// Make sure bottom coord is in bar range
	nBottomCoord = min(nBottomCoord, barBottom);

	// Ensure visible area is at least minimum height
	if (nBottomCoord - nTopCoord < INDICATOR_MIN_HEIGHT)
	{
		// If area is near top of file, add additional area to bottom
		// of the bar and vice versa.
		if (nTopCoord < Y_OFFSET + 20)
			nBottomCoord += INDICATOR_MIN_HEIGHT - (nBottomCoord - nTopCoord);
		else
		{
			// Make sure locationbox has min hight
			if ((nBottomCoord - nTopCoord) < INDICATOR_MIN_HEIGHT)
			{
				// If we have a high number of lines, it may be better
				// to keep the topline, otherwise the cursor can 
				// jump up and down unexpected
				nBottomCoord = nTopCoord + INDICATOR_MIN_HEIGHT;
			}
		}
	}

	// Store current values for later use (to check if area changes)
	m_visibleTop = nTopCoord;
	m_visibleBottom = nBottomCoord;

	CRect rcVisibleArea(2, m_visibleTop, rc.right - 2, m_visibleBottom);
	std::unique_ptr<CBitmap> pBitmap(CopyRectToBitmap(pClientDC, rcVisibleArea));
	std::unique_ptr<CBitmap> pDarkenedBitmap(GetDarkenedBitmap(pClientDC, pBitmap.get()));
	DrawBitmap(pClientDC, rcVisibleArea.left, rcVisibleArea.top, pDarkenedBitmap.get());
}

/**
 * @brief Public function for updating visible area indicator.
 *
 * @param [in] nTopLine New topline for indicator
 * @param [in] nBottomLine New bottomline for indicator
 */
void CLocationView::UpdateVisiblePos(int nTopLine, int nBottomLine)
{
	if (m_bDrawn)
	{
		CMergeDoc *pDoc = GetDocument();
		int nGroup = pDoc->GetActiveMergeView()->m_nThisGroup;
		int pane;
		IF_IS_TRUE_ALL(m_nSubLineCount[pane] == pDoc->GetView(nGroup, pane)->GetSubLineCount(), pane, pDoc->m_nBuffers)
		{
			int nTopCoord = static_cast<int>(Y_OFFSET +
					(static_cast<double>(nTopLine * m_lineInPix)));
			int nBottomCoord = static_cast<int>(Y_OFFSET +
					(static_cast<double>(nBottomLine * m_lineInPix)));
			if (m_visibleTop != nTopCoord || m_visibleBottom != nBottomCoord)
			{
				// Visible area was changed
				if (m_pSavedBackgroundBitmap)
				{
					CClientDC dc(this);
					CMyMemDC dcMem(&dc);
					// Clear previous visible rect
					DrawBitmap(&dcMem, 0, 0, m_pSavedBackgroundBitmap.get());

					DrawVisibleAreaRect(&dcMem, nTopLine, nBottomLine);
				}
			}
		}
		else
		{
			InvalidateRect(NULL);
			for (pane = 0; pane < pDoc->m_nBuffers; pane++)
				m_nSubLineCount[pane] = pDoc->GetView(nGroup, pane)->GetSubLineCount();
		}
	}
}

/**
 * @brief Unset pointers to MergeEditView when location pane is closed.
 */
void CLocationView::OnClose()
{
	CView::OnClose();
}

/** 
 * @brief Draw lines connecting moved blocks.
 */
void CLocationView::DrawConnectLines(CDC *pClientDC)
{
	CPen* oldObj = (CPen*)pClientDC->SelectStockObject(BLACK_PEN);

	POSITION pos = m_movedLines.GetHeadPosition();
	while (pos != NULL)
	{
		MovedLine line = m_movedLines.GetNext(pos);
		pClientDC->MoveTo(line.ptLeft.x, line.ptLeft.y);
		pClientDC->LineTo(line.ptRight.x, line.ptRight.y);
	}

	pClientDC->SelectObject(oldObj);
}

/** 
 * @brief Stores HWND of frame window (CChildFrame).
 */
void CLocationView::SetFrameHwnd(HWND hwndFrame)
{
	m_hwndFrame = hwndFrame;
}

/** 
 * @brief Request frame window to store sizes.
 *
 * When locationview size changes we want to save new size
 * for new windows. But we must do it through frame window.
 * @param [in] nType Type of resizing, SIZE_MAXIMIZED etc.
 * @param [in] cx New panel width.
 * @param [in] cy New panel height.
 */
void CLocationView::OnSize(UINT nType, int cx, int cy) 
{
	CView::OnSize(nType, cx, cy);

	// Height change needs block recalculation
	// TODO: Perhaps this should be determined from need to change bar size?
	// And we could change bar sizes more lazily, not from every one pixel change in size?
	if (cy != m_currentSize.cy)
		m_bRecalculateBlocks = TRUE;

	if (cx != m_currentSize.cx)
	{
		if (m_hwndFrame != NULL)
			::PostMessage(m_hwndFrame, MSG_STORE_PANESIZES, 0, 0);
	}

	m_currentSize.cx = cx;
	m_currentSize.cy = cy;
}

/** 
 * @brief Draw marker for top of currently selected difference.
 * This function draws marker for top of currently selected difference.
 * This marker makes it a lot easier to see where currently selected
 * difference is in location bar. Especially when selected diffence is
 * small and it is not easy to find it otherwise.
 * @param [in] pDC Pointer to draw context.
 * @param [in] yCoord Y-coord of top of difference, -1 if no difference.
 */
void CLocationView::DrawDiffMarker(CDC* pDC, int yCoord)
{
	int nBuffers = GetDocument()->m_nBuffers;

	CPoint points[3];
	points[0].x = m_bar[0].left - DIFFMARKER_WIDTH - 1;
	points[0].y = yCoord - DIFFMARKER_TOP;
	points[1].x = m_bar[0].left - 1;
	points[1].y = yCoord;
	points[2].x = m_bar[0].left - DIFFMARKER_WIDTH - 1;
	points[2].y = yCoord + DIFFMARKER_BOTTOM;

	COLORREF clrBlue = GetSysColor(COLOR_ACTIVECAPTION);
	CPen penDarkBlue(PS_SOLID, 0, GetDarkenColor(clrBlue, 0.9));
	CPen* oldObj = (CPen*)pDC->SelectObject(&penDarkBlue);
	CBrush brushBlue(clrBlue);
	CBrush* pOldBrush = pDC->SelectObject(&brushBlue);

	pDC->SetPolyFillMode(WINDING);
	pDC->Polygon(points, 3);

	points[0].x = m_bar[nBuffers - 1].right + 1 + DIFFMARKER_WIDTH;
	points[1].x = m_bar[nBuffers - 1].right + 1;
	points[2].x = m_bar[nBuffers - 1].right + 1 + DIFFMARKER_WIDTH;
	pDC->Polygon(points, 3);

	pDC->SelectObject(pOldBrush);
	pDC->SelectObject(oldObj);
}
