#include "include\souistd.h"
#include "control\SSpinButtonCtrl.h"

namespace SOUI
{
    SSpinButtonCtrl::SSpinButtonCtrl(void)
    :m_pDownSkin(GETBUILTINSKIN(SKIN_SYS_SPIN_DOWN))
    , m_pUpSkin(GETBUILTINSKIN(SKIN_SYS_SPIN_UP))
	, m_iActionBtnUp(WndState_Normal)
    , m_iActionBtnDown(WndState_Normal)
    ,m_nMin(0)
    , m_MaxLength(-1)
    ,m_nMax(100)
    ,m_nValue(0)
    ,m_uStep(1)
    ,m_bCircle(FALSE)
    , m_iBtnInterRight(0)
    {
        GetEventSet()->addEvent(EVENTID(EventSpinValue2String));
    }

    SSpinButtonCtrl::~SSpinButtonCtrl(void)
    {
    }

	void SSpinButtonCtrl::SetValue(int nValue)
	{
		m_nValue = nValue;
		OnValueChanged();
	}

	void SSpinButtonCtrl::SetRange(int nMin, int nMax)
	{
		m_nMin = nMin;
		m_nMax = nMax;
		if (m_nValue < m_nMin)
			m_nValue = m_nMin;
		if (m_nValue > nMax)
			m_nValue = nMax;
		OnValueChanged();
	}

	void SSpinButtonCtrl::SetStep(UINT nStep)
	{
		m_uStep = nStep;
	}

	//SWindow * SSpinButtonCtrl::GetBuddy()
	//{
	//	return GetParent()->FindChildByName(m_strBuddy);
	//}



    CSize SSpinButtonCtrl::GetDesiredSize(LPCRECT pRcContainer)
    {
        CSize szRet;
        if(GetLayoutParam()->IsSpecifiedSize(Horz))
        {
            szRet.cx = GetLayoutParam()->GetSpecifiedSize(Horz).toPixelSize(GetScale());
        }else
        {
            szRet.cx = m_pDownSkin->GetSkinSize().cx;
        }
        if(GetLayoutParam()->IsSpecifiedSize(Vert))
        {
            szRet.cy = GetLayoutParam()->GetSpecifiedSize(Vert).toPixelSize(GetScale());
        }else
        {
            szRet.cy = pRcContainer->bottom-pRcContainer->top;
        }

        return szRet;
    }


    HRESULT SSpinButtonCtrl::OnAttrValue(const SStringW& strValue, BOOL bLoading)
    {
        m_nValue = _wtoi(strValue);
        if(!bLoading)
        {
            OnValueChanged();
        }
        return S_OK;
    }

    HRESULT SSpinButtonCtrl::OnTxNotify(DWORD iNotify, LPVOID pv)
    {

        if (iNotify != EN_CHANGE && iNotify != EN_KILLFOCUS)
            return __super::OnTxNotify(iNotify, pv);

        SStringT str = GetWindowText();

        do
        {
            int tempscale = _ttoi(str);
            if (iNotify == EN_KILLFOCUS)
            {
                if (tempscale < m_nMin)
                {
                    tempscale = m_nMin;
                    str = SStringT().Format(_T("%d"), tempscale);
                    SetWindowText(str);
                }
                else if (tempscale > m_nMax)
                {
                    tempscale = m_nMax;
                    str = SStringT().Format(_T("%d"), tempscale);
                    SetWindowText(str);

                }
                
            }
            else
            {
                //限制.和-
                {
                    int indexdot = str.Find(_T("."));
                    int indexcut = str.Find(_T("-"));
                    if (indexdot != -1)
                    {
                        str = m_strOld;
                        SetWindowText(m_strOld);
                        SetSel(-1, TRUE);
                        break;
                    }
                    else if (indexcut != -1)
                    {
                        m_strOld = str = SStringT().Format(_T("%d"), m_nMin);
                        SetWindowText(str);
                        KillFocus();
                        break;
                    }
                    else
                    {
                        if (m_MaxLength != -1 && str.GetLength() >= m_MaxLength)
                        {
                            int num = _ttoi(str);
                            if (num > m_nMax)
                            {
                                num = m_nMax;
                            }
                            else if (num < m_nMin)
                            {
                                num = m_nMin;
                            }
                            str = SStringT().Format(_T("%d"), num);
                            KillFocus();
                            CheckBtnDisable();
                            break;
                        }

                    }
                }
                //tempscale = _ttoi(str);
            }
            //m_nValue = tempscale;
            
        } while (0);
        m_strOld = str;
        CheckBtnDisable();

        return __super::OnTxNotify(iNotify, pv);
    }

    void SSpinButtonCtrl::OnValueChanged(bool bInit)
    {
        //SWindow *pBuddy = GetBuddy();
   //     if(pBuddy) 
   //     {
   //         EventSpinValue2String evt(this);
			//evt.bInit=bInit;
   //         evt.nValue = m_nValue;
   //         evt.strValue = SStringT().Format(_T("%d"),m_nValue);
   //         FireEvent(evt);
   //         pBuddy->SetWindowText(evt.strValue);

            //int tempnum = m_nValue;
            //if (++tempnum >= m_nMax)
            //{
            //    tempnum = m_nMax;
            //}
            //m_nValue = tempnum;

            GetEventSet()->setMutedState(true);
            SetWindowText(SStringT().Format(_T("%d"), m_nValue));
            GetEventSet()->setMutedState(false);
        //}
    }


	void SSpinButtonCtrl::OnLButtonDown(UINT nFlags, CPoint point)
	{
        Btn btn = CheckInUpBtn(point);
        if(btn != Btn::NOBTN)OnClick(btn);
		__super::OnLButtonDown(nFlags, point);
        //SetSel(-1);
		//Invalidate();
		//SetTimer(1, 200);
	}

	void SSpinButtonCtrl::OnLButtonUp(UINT nFlags, CPoint point)
	{
		__super::OnLButtonUp(nFlags, point);
        m_iActionBtnUp = ChangeBtnState(0, WndState_PushDown, m_iActionBtnUp);
        m_iActionBtnDown = ChangeBtnState(0, WndState_PushDown, m_iActionBtnDown);
		//KillTimer(1);
		Invalidate();
	}

	void SSpinButtonCtrl::OnMouseMove(UINT nFlags, CPoint point)
	{
        __super::OnMouseMove(nFlags, point);
        if (CheckInUpBtn(point) == Btn::UPBTN)
        {
            m_iActionBtnUp = ChangeBtnState(WndState_Hover,0,m_iActionBtnUp);
            m_iActionBtnDown  = ChangeBtnState(WndState_Normal, 0, m_iActionBtnDown);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        else if(CheckInUpBtn(point) == Btn::DOWNBTN)
        {
            m_iActionBtnUp = ChangeBtnState(WndState_Normal, 0, m_iActionBtnUp);
            m_iActionBtnDown = ChangeBtnState(WndState_Hover, 0, m_iActionBtnDown);
            SetCursor(LoadCursor(NULL, IDC_ARROW));
        }
        else
        {
            if (m_iActionBtnUp & WndState_Disable)
            {
                m_iActionBtnUp = ChangeBtnState(WndState_Disable, 0xff, m_iActionBtnUp);
            }
            else
            {
                m_iActionBtnUp = ChangeBtnState(0, WndState_Hover, m_iActionBtnUp);
            }

            if (m_iActionBtnDown & WndState_Disable)
            {
                m_iActionBtnDown = ChangeBtnState(WndState_Disable, 0xff, m_iActionBtnDown);
            }
            else
            {
                m_iActionBtnDown = ChangeBtnState(0, WndState_Hover, m_iActionBtnDown);
            }
        }
        Invalidate();
	}


    void SSpinButtonCtrl::OnColorize(COLORREF cr)
    {
        __super::OnColorize(cr);

        if ( m_pUpSkin ) m_pUpSkin->OnColorize(cr);
        if ( m_pDownSkin ) m_pDownSkin->OnColorize(cr);
    }


	void SSpinButtonCtrl::OnPaint(IRenderTarget *pRT)
	{
		__super::OnPaint(pRT);
		CRect rcClient = GetClientRect();
		//CRect rcUp = rcClient, rcDown = rcClient;
		//rcUp.bottom = rcDown.top = rcClient.top + rcClient.Height() / 2;
        CRect rcUp = m_rcUp, rcDown = m_rcDown;
        rcUp.OffsetRect(rcClient.left, rcClient.top);
        rcDown.OffsetRect(rcClient.left, rcClient.top);
		DWORD iState = IIF_STATE4(GetState(), 0, 1, 2, 3);

		m_pUpSkin->Draw(pRT, rcUp, IIF_STATE4(m_iActionBtnUp, 0, 1, 2, 3));

		m_pDownSkin->Draw(pRT, rcDown, IIF_STATE4(m_iActionBtnDown, 0, 1, 2, 3));


	}
    void SSpinButtonCtrl::OnSize(UINT nType, CSize size)
    {
        __super::OnSize(nType, size);
        CSize EditSize = this->GetClientRect().Size();

        int size_x;
        int size_y;

        //适配padding属性
        CRect paddingRc = m_style.GetPadding();
        //int offset_x = -paddingRc.left + paddingRc.right;
        //int offset_y = -paddingRc.top + paddingRc.bottom;
        CSize SkinSize = m_pUpSkin->GetSkinSize();
        size_x = m_BtnSize.cx ? m_BtnSize.cx : SkinSize.cx;
        size_y = m_BtnSize.cy ? m_BtnSize.cy : SkinSize.cy;
        int up_pos_x = EditSize.cx - (m_iBtnInterRight + size_x) /*+ offset_x*/;
        int down_pos_x = EditSize.cx - (m_iBtnInterRight + size_x)/* + offset_x*/;
        int up_pos_y = EditSize.cy / 4 - size_y / 2;
        int down_pos_y = EditSize.cy * 3 / 4 - size_y / 2;

        m_rcUp = CRect(up_pos_x, up_pos_y, up_pos_x + size_x, up_pos_y + size_y);
        m_rcDown = CRect(down_pos_x, down_pos_y, down_pos_x + size_x, down_pos_y + size_y);
        UpdateLayout();
    }

    HRESULT SSpinButtonCtrl::OnAttrSize(const SStringW & strValue, BOOL bLoading)
    {
        SStringWList szStr;
        if (2 != SplitString(strValue, L',', szStr)) return E_FAIL;

        m_BtnSize = CSize(_ttoi(szStr[0]), _ttoi(szStr[1]));

        return E_NOTIMPL;
    }

	//bool SSpinButtonCtrl::OnBuddyChange(EventArgs* pEvt)
	//{
	//	SWindow *pBuddy = GetBuddy();
	//	if(pBuddy)
	//	{
	//		int nValue = _ttoi(pBuddy->GetWindowText());
	//		if (nValue <= m_nMax && nValue >= m_nMin)
	//		{
	//			m_nValue = nValue;

	//			EventSpinValue2String evt(this);
	//			evt.bInit = false;
	//			evt.nValue = m_nValue;
	//			evt.strValue = SStringT().Format(_T("%d"), m_nValue);
	//			FireEvent(evt);
	//		}
	//	}
	//	return true;
	//}

	int SSpinButtonCtrl::OnCreate(void *)
	{
		int nRet =__super::OnCreate(NULL);
		if (nRet != 0) return nRet;
		OnValueChanged(true);
        OnAttrNotifyChange(_T("1"),FALSE);
        DefAttributeProc(_T("number"), _T("1"), FALSE);
        CheckBtnDisable();
		//SWindow *pBuddy = GetBuddy();
		//if (pBuddy)	pBuddy->GetEventSet()->subscribeEvent(EVT_RE_NOTIFY, Subscriber(&SSpinButtonCtrl::OnBuddyChange, this));
		return 0;
	}

	void SSpinButtonCtrl::OnClick(Btn btn)
	{
		//SASSERT(m_iActionBtn != ACTION_NULL);
		if (btn == Btn::DOWNBTN)
		{
            m_iActionBtnDown = ChangeBtnState(WndState_PushDown, 0, m_iActionBtnDown);
			m_nValue -= m_uStep;
			if (m_nValue<m_nMin)
			{
				if (m_bCircle) m_nValue = m_nMax;
				else m_nValue = m_nMin;
			}
            SetCursor(LoadCursor(NULL, IDC_ARROW));
		}
		else if(btn == Btn::UPBTN)
		{
            m_iActionBtnUp = ChangeBtnState(WndState_PushDown, 0, m_iActionBtnUp);
			m_nValue += m_uStep;
			if (m_nValue>m_nMax)
			{
				if (m_bCircle) m_nValue = m_nMin;
				else m_nValue = m_nMax;
			}
            SetCursor(LoadCursor(NULL, IDC_ARROW));
		}
		OnValueChanged();
        CheckBtnDisable();
        KillFocus();
	}

    DWORD SSpinButtonCtrl::ChangeBtnState(DWORD dwStateAdd, DWORD dwStateRemove, DWORD oldState)
    {
        DWORD dwOldState = oldState;

        DWORD dwNewState = oldState;
        dwNewState &= ~dwStateRemove;
        dwNewState |= dwStateAdd;
        Invalidate();
        return dwNewState;
    }

    void SSpinButtonCtrl::CheckBtnDisable()
    {
        do 
        {
            if (m_nValue >= m_nMax && !m_bCircle)
            {
                m_iActionBtnUp = ChangeBtnState(WndState_Disable, 0, m_iActionBtnUp);
            }
            else
            {
                m_iActionBtnUp = ChangeBtnState(0, 0xFF, m_iActionBtnUp);
            }


            if (m_nValue <= m_nMin && !m_bCircle)
            {
                m_iActionBtnDown = ChangeBtnState(WndState_Disable, 0, m_iActionBtnDown);
            }
            else
            {
                m_iActionBtnDown = ChangeBtnState(0, 0xFF, m_iActionBtnDown);
            }
        } while (0);

    }

    SSpinButtonCtrl::Btn SSpinButtonCtrl::CheckInUpBtn(CPoint point)
    {
        CRect rcClient = GetClientRect();
        CRect rcUp = m_rcUp, rcDown = m_rcDown;
        rcUp.OffsetRect(rcClient.left, rcClient.top);
        rcDown.OffsetRect(rcClient.left, rcClient.top);
        Btn btn = NOBTN;
        if (rcClient.PtInRect(point))
        {
            if (rcUp.PtInRect(point))
                btn = UPBTN;
            else if (rcDown.PtInRect(point))
                btn = DOWNBTN;
            else
                btn = NOBTN;
        }
        else
        {
            btn = NOBTN;
        }

        return btn;
    }

    void SSpinButtonCtrl::SetWindowText(LPCTSTR lpszText)
    {
        __super::SetWindowText(lpszText);
        m_nValue = _ttoi(lpszText);
    }

    void SSpinButtonCtrl::OnMouseLeave()
    {
        CheckBtnDisable();
    }

}
