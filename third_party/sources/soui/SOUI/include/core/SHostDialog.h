/**
* Copyright (C) 2014-2050 
* All rights reserved.
* 
* @file       SHostDialog.h
* @brief      
* @version    v1.0      
* @author     SOUI group   
* @date       2014/08/02
* 
* Describe    SOUI的Dialog模块
*/

#pragma once

#include "shostwnd.h"
#include "smsgloop.h"

namespace SOUI
{
    class SOUI_EXP SHostDialog : public SHostWnd
    {
        SOUI_CLASS_NAME(SHostDialog,L"hostdlg")
    public:
        SHostDialog(LPCTSTR pszXmlName);
        ~SHostDialog(void);
        
        virtual INT_PTR DoModal(HWND hParent=NULL);
        
        virtual void EndDialog(INT_PTR nResult);

		static void DoModalStackUse(IN bool blUse);
		static void DoModalStackUpdate();
		static void DoModalStackRemove(IN SHostDialog* hWnd);
		static SHostDialog* DoModalStackTop();
		static SHostDialog* DoModalStackTopEnable(IN BOOL blEnable);
		static void DoModalStackAdd(IN SHostDialog* hWnd);
		static void DoModalStackEndialogAll(IN int nRetSult);
		static void DoModalStackEndialog(IN SHostDialog* pDlg, IN int nRetsult);
	protected:
        void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);
		
		void OnOK();
		void OnCancel();
		void OnClose();
        void OnDestroy();
        virtual SMessageLoop * GetMsgLoop(){return m_MsgLoop;}
        
        EVENT_MAP_BEGIN()
			EVENT_ID_COMMAND(IDCLOSE, OnClose)
			EVENT_ID_COMMAND(IDOK, OnOK)
            EVENT_ID_COMMAND(IDCANCEL,OnCancel)
        EVENT_MAP_END()

        BEGIN_MSG_MAP_EX(SHostDialog)
			MSG_WM_DESTROY(OnDestroy)
			MSG_WM_CLOSE(OnCancel)
            MSG_WM_KEYDOWN(OnKeyDown)
            CHAIN_MSG_MAP(SHostWnd)
            REFLECT_NOTIFICATIONS_EX()
        END_MSG_MAP()

        INT_PTR m_nRetCode;
        
        SMessageLoop * m_MsgLoop;

		static SArray<SHostDialog*> m_doModalStack;//弹框的栈
		static bool m_blDoModalStackUse;//使用弹框的栈功能
    };
}
