#include "souistd.h"
#include "control/SHotKeyCtrl.h"
#include <CommCtrl.h>

namespace SOUI
{

SHotKeyCtrl::SHotKeyCtrl(void)
{
    m_wInvalidModifier=0;
    m_wInvalidComb=HKCOMB_NONE;
    m_bInSetting=FALSE;
    m_bFocusable=TRUE;
	m_bWantTab = FALSE;
	m_evtSet.addEvent(EVENTID(EventSetHotKey));
	//Neko,优化,增加解决快捷按键按下完毕了之后可以立刻进行注册快捷键
	m_evtSet.addEvent(EVENTID(EventSetHotKeyUp));
	//Neko,修复中文情况下使用快捷按键
	m_nKeyDownKey = 0;
	m_hCurIMC = NULL;
	m_nIMEDisable = 0;
}

SHotKeyCtrl::~SHotKeyCtrl(void)
{
}

int SHotKeyCtrl::OnCreate( LPVOID )
{
    int nRet=__super::OnCreate(NULL);
    if(nRet!=0) return nRet;
    
    CAutoRefPtr<IRenderTarget> pRT;
    GETRENDERFACTORY->CreateRenderTarget(&pRT,0,0);
    BeforePaintEx(pRT);
    m_curFont=(IFont*)pRT->GetCurrentObject(OT_FONT);
    return 0;
}

void SHotKeyCtrl::OnLButtonDown( UINT nFlags,CPoint pt )
{
    __super::OnLButtonDown(nFlags,pt);
    GetContainer()->OnSetSwndFocus(m_swnd);
}

void SHotKeyCtrl::OnPaint( IRenderTarget * pRT )
{
    SPainter painter;
    BeforePaint(pRT,painter);
    CRect rcClient;
    GetTextRect(&rcClient);
    SStringT str=GetWindowText();
	UINT uAlign = GetTextAlign();
    pRT->DrawText(str,str.GetLength(),&rcClient, uAlign);
    AfterPaint(pRT,painter);
}

void SHotKeyCtrl::UpdateCaret()
{
    SStringT str=GetWindowText();
    IRenderTarget *pRT=GetRenderTarget(NULL,OLEDC_NODRAW);
    CAutoRefPtr<IFont> oldFont;
    pRT->SelectObject(m_curFont,(IRenderObj**)&oldFont);
    SIZE szTxt;
    pRT->MeasureText(str,str.GetLength(),&szTxt);
    pRT->SelectObject(oldFont);
    ReleaseRenderTarget(pRT);
    
    CRect rcClient;
    GetTextRect(&rcClient);

	UINT uAlign = GetTextAlign();
	int x = rcClient.left + szTxt.cx;
	int y = rcClient.top + (rcClient.Height() - szTxt.cy) / 2;
	if (uAlign & DT_CENTER)
		x += (rcClient.Width() - szTxt.cx) / 2;
	else if (uAlign & DT_RIGHT)
		x = rcClient.right;

    GetContainer()->OnSetCaretPos(x,y);
}

UINT SHotKeyCtrl::GetTextAlign()
{
	UINT uAlign = SWindow::GetTextAlign();
	uAlign &= ~DT_BOTTOM;
	uAlign |= DT_VCENTER | DT_SINGLELINE;
	return uAlign;
}

void SHotKeyCtrl::OnSetFocus(SWND wndOld)
{
	//Neko(增加),禁用IME,默认启用,和密码控件一样,默认情况下输入法可以使用,0表示启用输入法,1表示禁用输入法
	if (m_nIMEDisable > 0)
	{
		HWND hWnd = GetContainer()->GetHostHwnd();
		//HIMC hImc = ImmGetContext(hWnd);
		//if (NULL == hImc)
		//{
		//	hImc = ImmCreateContext();
		//	ImmAssociateContext(hWnd, hImc);
		//}
		HIMC hImc = ImmAssociateContext(hWnd, NULL);
		if (NULL == m_hCurIMC)
		{
			m_hCurIMC = hImc;
		}
	}
    IRenderTarget *pRT=GetRenderTarget(NULL,OLEDC_NODRAW);
    CAutoRefPtr<IFont> oldFont;
    pRT->SelectObject(m_curFont,(IRenderObj**)&oldFont);
    SIZE szTxt;
    pRT->MeasureText(_T("A"),1,&szTxt);
    pRT->SelectObject(oldFont);
    ReleaseRenderTarget(pRT);
    
    CreateCaret(NULL,1,szTxt.cy);

    UpdateCaret();
    GetContainer()->OnShowCaret(TRUE);
    
    __super::OnSetFocus(wndOld);
    
}

void SHotKeyCtrl::OnKillFocus(SWND wndFocus)
{
	//Neko(增加),禁用IME,默认启用,和密码控件一样,默认情况下输入法可以使用,0表示启用输入法,1表示禁用输入法
	if (m_nIMEDisable > 0)
	{
		HWND hWnd = GetContainer()->GetHostHwnd();
		//HIMC hImc = ImmGetContext(hWnd);
		//ImmAssociateContext(hWnd, NULL);
		//if (hImc)
		//{
		//	ImmDestroyContext(hImc);
		//}
		if (m_hCurIMC != NULL)
		{
			ImmAssociateContext(hWnd, m_hCurIMC);
		}
		m_hCurIMC = NULL;
	}
    GetContainer()->OnShowCaret(FALSE);
    __super::OnKillFocus(wndFocus);
}

void SHotKeyCtrl::UpdateModifier()
{
    BOOL bAlt=GetKeyState(VK_MENU)&0x8000;
    BOOL bCtrl=GetKeyState(VK_CONTROL)&0x8000;
    BOOL bShift=GetKeyState(VK_SHIFT)&0x8000;

    WORD wCombKey=0;
    if(!bAlt && !bCtrl && !bShift) wCombKey=HKCOMB_NONE,m_wModifier=0;
    else if(bAlt && !bCtrl && !bShift) wCombKey=HKCOMB_A,m_wModifier=MOD_ALT;
    else if(!bAlt && bCtrl && !bShift) wCombKey=HKCOMB_C,m_wModifier=MOD_CONTROL;
    else if(!bAlt && !bCtrl && bShift) wCombKey=HKCOMB_S,m_wModifier=MOD_SHIFT;
    else if(bAlt && bCtrl && !bShift) wCombKey=HKCOMB_CA,m_wModifier=MOD_ALT|MOD_CONTROL;
    else if(bAlt && !bCtrl && bShift) wCombKey=HKCOMB_SA,m_wModifier=MOD_SHIFT|MOD_ALT;
    else if(!bAlt && bCtrl && bShift) wCombKey=HKCOMB_SC,m_wModifier=MOD_SHIFT|MOD_CONTROL;
    else wCombKey=HKCOMB_SCA,m_wModifier=MOD_ALT|MOD_SHIFT|MOD_CONTROL;
    if(wCombKey&m_wInvalidComb)
        m_wModifier=m_wInvalidModifier;

	EventSetHotKey ev(this);
	ev.wModifiers = m_wModifier;
	ev.vKey = m_wVK;
	FireEvent(ev);
}

void SHotKeyCtrl::OnKeyDown( UINT nChar, UINT nRepCnt, UINT nFlags )
{
	//Neko,修复中文情况下使用快捷按键,开始
	if (VK_PROCESSKEY == nChar )
	{
		m_nKeyDownKey = nChar;//保存KeyDown的虚拟键盘码
		//如果是 VK_PROCESSKEY,表示开启中文输入,需要转码,使得WM_KEYUP可以正确获取虚拟按键
		ImmGetVirtualKey(this->GetContainer()->GetHostHwnd());
	}
	else
	{
		m_nKeyDownKey = 0;
	}
	//Neko,修复中文情况下使用快捷按键,结束
	
    if(FALSE == m_bInSetting)
    {
        m_bInSetting=TRUE;
        m_wVK=0;
        m_wModifier=m_wInvalidModifier;
    }
    SStringT strKey=GetKeyName(nChar);
    if(false == strKey.IsEmpty())
    {
        //合法的按键,则保存
        m_wVK=nChar;
    }
    UpdateModifier();
    UpdateCaret();
    Invalidate();
}

void SHotKeyCtrl::OnKeyUp( UINT nChar, UINT nRepCnt, UINT nFlags )
{
    if(FALSE == m_bInSetting) return;

	//Neko,修复中文情况下使用快捷按键,开始
	if (VK_PROCESSKEY == m_nKeyDownKey)
	{
		SStringT strKey = GetKeyName(nChar);
		if (false == strKey.IsEmpty())
		{
			//合法的按键,则保存
			m_wVK = nChar;
		}
	}
	//Neko,修复中文情况下使用快捷按键,结束

    if(nChar == m_wVK)
    {
        m_bInSetting=FALSE;//WM_KEYDOWN 和 WM_KEYUP一致的情况下才重置
    }
    else if(m_wVK==0 && (GetKeyState(VK_SHIFT)&0x8000)==0 && (GetKeyState(VK_MENU)&0x8000)==0 && (GetKeyState(VK_CONTROL)&0x8000)==0)
    {
		//如果 VK_SHIFT 或 VK_MENU 或 VK_CONTROL 按下的情况下也重置
        m_bInSetting=FALSE;
        UpdateModifier();
        UpdateCaret();
        Invalidate();
    }
    else if(nChar==VK_SHIFT || nChar==VK_MENU || nChar== VK_CONTROL)
    {
        UpdateModifier();
        UpdateCaret();
        Invalidate();
    }
	//Neko,修复中文情况下使用快捷按键,开始
	if (VK_PROCESSKEY == m_nKeyDownKey)
	{
		m_nKeyDownKey = 0;
		m_bInSetting = FALSE;
		UpdateModifier();
		UpdateCaret();
		Invalidate();
	}
	//Neko,修复中文情况下使用快捷按键,结束

	EventSetHotKeyUp ev(this);
	ev.wModifiers = m_wModifier;
	ev.vKey = m_wVK;
	FireEvent(ev);
}

void SHotKeyCtrl::OnSysKeyDown( UINT nChar, UINT nRepCnt, UINT nFlags )
{
    if(GetKeyState(VK_MENU)&0x8000) OnKeyDown(nChar,nRepCnt,nFlags);
    else SetMsgHandled(FALSE);
}

void SHotKeyCtrl::OnSysKeyUp(UINT nChar, UINT nRepCnt, UINT nFlags)
{
    if(nChar==VK_MENU || GetKeyState(VK_MENU)&0x8000) OnKeyUp(nChar,nRepCnt,nFlags);
    else SetMsgHandled(FALSE);
}

void SHotKeyCtrl::SetRule( WORD wInvalidComp,WORD wModifier )
{
    m_wInvalidComb=wInvalidComp;
    m_wInvalidModifier=wModifier;
}

void SHotKeyCtrl::SetHotKey( WORD vKey,WORD wModifiers )
{
    m_wVK=vKey;
    m_wModifier=wModifiers;
    UpdateCaret();
    Invalidate();
}

void SHotKeyCtrl::GetHotKey( WORD & vKey,WORD &wModifers )
{
    vKey=m_wVK;
    wModifers=m_wModifier;
}

SStringT SHotKeyCtrl::GetWindowText(BOOL bRawText)
{
	return FormatHotkey();
}

}//namespace SOUI
