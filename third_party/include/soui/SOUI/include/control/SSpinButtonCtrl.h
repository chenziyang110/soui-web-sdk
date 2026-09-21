#pragma once
#include "core/SWnd.h"

/*=================================================================
 * @brief
 *      SSpinButtonCtrl控件
 * @author
 *      FZG
 * @date
 *      2023/11/24
 * @remark
 *    属性min：设置最小值
 *    属性max：设置最大值
 *    属性upSkin： 上按钮皮肤
 *    属性downSkin:  下按钮皮肤
 *    属性BtnSize: 按钮大小（一般认为两个按钮大小应该一致）
 *    属性interRight: 按钮距离右边距离
 *    属性step：   每次增加数值
 *    属性circle： 是否循环
 * @code
 *     xml里面用法：
 *        <spinButton pos="100,[50"  size="100,60" maxbuf="3" btnsize="10,10" interRight="4" padding="6,0,0,0" class="ft-wryh-13--ct-333333" name="edit_gif" downSkin="ic_down" upskin="ic_up" skin="edit_e0e0e0" text="30">
 *
 *     代码里面：
 *     方法1：
 *     EVENT_ID_HANDLER(R.id.edit_gif, EVT_RE_NOTIFY, OnUpdateParam)监听事件
 *     OnUpdateParam(){if(pReEvt->iNotify != EN_KILLFOCUS)return; //每次失去焦点刷新数据 } 可以过滤EN_CHANGE和EN_KILLFOCUS，也可以单独过滤EN_KILLFOCUS
 *     方法2:
 *     调用GetValue()获取值
 *
 *
 * @endcode
**=================================================================*/

namespace SOUI
{
    class SOUI_EXP SSpinButtonCtrl : public SEdit
    {
    SOUI_CLASS_NAME(SSpinButtonCtrl,L"spinButton")

    typedef enum ActionButton {
        ACTION_NULL = -1,
        ACTION_HOVER,
        ACTION_CLICK,
        ACTION_DISABLE
    }ActionButton;

    typedef enum Btn
    {
        NOBTN = -1,
        DOWNBTN,
        UPBTN
    }Btn;
    public:
        SSpinButtonCtrl(void);
        ~SSpinButtonCtrl(void);
        
		void SetValue(int nValue);
		void SetRange(int nMin, int nMax);
		void SetStep(UINT nStep);
        int GetValue() { return m_nValue; };
		//SWindow * GetBuddy();

    protected:
        virtual CSize GetDesiredSize(LPCRECT pRcContainer);
		virtual BOOL NeedRedrawWhenStateChange() { return TRUE; }
        virtual void OnColorize(COLORREF cr);

    protected:
        void OnValueChanged(bool bInit=false);
        virtual HRESULT OnTxNotify(DWORD iNotify, LPVOID pv);
		void OnClick(Btn btn);
        DWORD ChangeBtnState(DWORD dwStateAdd, DWORD dwStateRemove, DWORD oldState);
        void CheckBtnDisable();
        Btn CheckInUpBtn(CPoint point);
        virtual void SetWindowText(LPCTSTR lpszText);

        void OnPaint(IRenderTarget *pRT);
        void OnSize(UINT nType, CSize size);
        //void OnTimer(char cTimerId);
        //bool OnBuddyChange(EventArgs * pEvt);
        int OnCreate(void *);
        void OnLButtonDown(UINT nFlags, CPoint point);
        void OnLButtonUp(UINT nFlags, CPoint point);
        void OnMouseMove(UINT nFlags, CPoint point);
        void OnMouseLeave();
		SOUI_MSG_MAP_BEGIN()
			MSG_WM_CREATE(OnCreate)
			MSG_WM_LBUTTONDOWN(OnLButtonDown)
			MSG_WM_LBUTTONDBLCLK(OnLButtonDown)
			MSG_WM_MOUSEMOVE(OnMouseMove)
            MSG_WM_MOUSELEAVE(OnMouseLeave)
			MSG_WM_LBUTTONUP(OnLButtonUp)
			MSG_WM_PAINT_EX(OnPaint)
            MSG_WM_SIZE(OnSize)
			//MSG_WM_TIMER_EX(OnTimer)
		SOUI_MSG_MAP_END()

    protected:
        HRESULT OnAttrValue(const SStringW& strValue, BOOL bLoading);
        HRESULT OnAttrSize(const SStringW & strValue, BOOL bLoading);
		SOUI_ATTRS_BEGIN()
			ATTR_INT(L"max", m_nMax, FALSE)
			ATTR_INT(L"min", m_nMin, FALSE)
            ATTR_INT(L"MaxBuf", m_MaxLength, FALSE)
			ATTR_CUSTOM(L"value", OnAttrValue)
			ATTR_UINT(L"step", m_uStep, FALSE)
			ATTR_INT(L"circle", m_bCircle, FALSE)
			//ATTR_STRINGW(L"buddy", m_strBuddy, FALSE)
			ATTR_SKIN(L"upskin", m_pUpSkin, TRUE)
			ATTR_SKIN(L"downSkin",m_pDownSkin,TRUE)
            ATTR_INT(L"InterRight", m_iBtnInterRight, TRUE)
            ATTR_CUSTOM(L"BtnSize", OnAttrSize)
        SOUI_ATTRS_END()
        
	protected:
        int m_nMax;
        int m_nMin;
        int m_nValue;                   //当前数值
        UINT m_uStep;
        BOOL m_bCircle;
        SStringW m_strBuddy;
        
		CAutoRefPtr<ISkinObj> m_pUpSkin;
		CAutoRefPtr<ISkinObj> m_pDownSkin;


		DWORD			  m_iActionBtnUp;   //上按钮状态
        DWORD			  m_iActionBtnDown;//下按钮状态
        CRect                     m_rcUp;
        Btn                       m_BtnNow; //记录当前按钮
        CRect                     m_rcDown;
        CSize         m_BtnSize;        //按钮大小
        int m_iBtnInterRight;           //按钮距离右边距
        SStringT m_strOld;              //存储旧字符串
        int m_MaxLength;                //限制长度
    };
}
