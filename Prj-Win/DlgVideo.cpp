/*
*  Copyright (c) 2016 The AnyRTC project authors. All Rights Reserved.
*
*  Please visit https://www.anyrtc.io for detail.
*
* The GNU General Public License is a free, copyleft license for
* software and other kinds of works.
*
* The licenses for most software and other practical works are designed
* to take away your freedom to share and change the works.  By contrast,
* the GNU General Public License is intended to guarantee your freedom to
* share and change all versions of a program--to make sure it remains free
* software for all its users.  We, the Free Software Foundation, use the
* GNU General Public License for most of our software; it applies also to
* any other work released this way by its authors.  You can apply it to
* your programs, too.
* See the GNU LICENSE file for more info.
*/
#include "stdafx.h"
#include "DlgVideo.h"

extern int gLiving;
// DlgVideo 对话框

IMPLEMENT_DYNAMIC(DlgVideo, CDialog)

DlgVideo::DlgVideo(CWnd* pParentWnd)
	: CDialog(DlgVideo::IDD, pParentWnd)
{
}

DlgVideo::~DlgVideo()
{
}

void DlgVideo::DoDataExchange(CDataExchange* pDX)
{
	CDialog::DoDataExchange(pDX);
}


BEGIN_MESSAGE_MAP(DlgVideo, CDialog)
	ON_WM_CLOSE()
	ON_WM_ERASEBKGND()
	ON_WM_LBUTTONDBLCLK()
	ON_WM_SIZE()
	ON_WM_SHOWWINDOW()
END_MESSAGE_MAP()


// DlgVideo 消息处理程序

void DlgVideo::OnOK()
{
}

void DlgVideo::OnCancel()
{
}

void DlgVideo::OnClose()
{
}

BOOL DlgVideo::OnInitDialog()
{
	CDialog::OnInitDialog();


	return TRUE;  // return TRUE unless you set the focus to a control
	// 异常: OCX 属性页应返回 FALSE
}

void DlgVideo::OnPaint() {
}

BOOL DlgVideo::DestroyWindow()
{

	return CDialog::DestroyWindow();
}

BOOL DlgVideo::OnEraseBkgnd(CDC* pDC)
{
	return FALSE;
}

void DlgVideo::OnLButtonDblClk(UINT nFlags, CPoint point)
{
	//m_rDlgMain.OnFullScreen(this);

	CDialog::OnLButtonDblClk(nFlags, point);
}


void DlgVideo::OnSize(UINT nType, int cx, int cy)
{
	CDialog::OnSize(nType, cx, cy);

	// TODO: 在此处添加消息处理程序代码
}


void DlgVideo::OnShowWindow(BOOL bShow, UINT nStatus)
{
	CDialog::OnShowWindow(bShow, nStatus);
	if (bShow)
		gLiving = 1;
	else
		gLiving = 0;
	// TODO: 在此处添加消息处理程序代码
}
