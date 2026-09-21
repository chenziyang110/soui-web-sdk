#include "souistd.h"
#include "core/SHostDialog.h"
#include "core/SMsgLoop.h"

namespace SOUI
{
	SArray<SHostDialog*> SHostDialog::m_doModalStack;//弹框的栈
	bool SHostDialog::m_blDoModalStackUse = false;
    #define RC_INIT 0xcccccccc

    SHostDialog::SHostDialog(LPCTSTR pszXmlName):SHostWnd(pszXmlName),m_nRetCode(RC_INIT)
    {
        m_MsgLoop = SApplication::getSingleton().GetMsgLoopFactory()->CreateMsgLoop();
    }

    SHostDialog::~SHostDialog(void)
    {
        SApplication::getSingleton().GetMsgLoopFactory()->DestoryMsgLoop(m_MsgLoop);
    }

    INT_PTR SHostDialog::DoModal(HWND hParent/*=NULL*/)
    {
		HWND hWndLastActive = ::GetActiveWindow();
        if(!hParent)
        {
            //hParent = ::GetActiveWindow();
			hParent = hWndLastActive;
        }
               
        BOOL bEnableParent = FALSE;
        if (hParent && hParent != ::GetDesktopWindow() && ::IsWindowEnabled(hParent))
        {
            ::EnableWindow(hParent, FALSE);
            bEnableParent = TRUE;
        }
		//禁用上一个Domodal的窗体
		SHostDialog* hWndDomodalPre = SHostDialog::DoModalStackTopEnable(FALSE);
        
		if (!Create(hParent, WS_POPUP | WS_CLIPCHILDREN, 0, 0, 0, 0, 0))
		{
			if (bEnableParent)
			{
				::EnableWindow(hParent, TRUE);
			}
			return 0;
		}

		//Neko,SOUI的DPI适配增加修改
		this->CenterWindow(hParent);

        CSimpleWnd::SendMessage(WM_INITDIALOG, (WPARAM)m_hWnd);
        
        if(m_nRetCode == RC_INIT)
        {
			//Neko(修复),SetActiveWindow的返回值不一致,由于 Create 之后导致的,返回的是创建后的对话框的值
			//hWndLastActive 所以提到上面去
            //HWND hWndLastActive = ::SetActiveWindow(m_hWnd);
			::SetActiveWindow(m_hWnd);

            if(GetExStyle()&WS_EX_TOOLWINDOW)
                ::ShowWindow(m_hWnd,SW_SHOWNOACTIVATE);
            else
                ::ShowWindow(m_hWnd,SW_SHOWNORMAL);

			//添加Domodal的窗体栈
			SHostDialog::DoModalStackAdd(this);
            int nRet = m_MsgLoop->Run();
			//移除Domodal的窗体栈
			SHostDialog::DoModalStackRemove(this);

			if(m_nRetCode == RC_INIT)
			{//不是自己主动使用EndDialog关闭窗口，重新把WM_QUIT放回消息队列。
				PostQuitMessage(nRet);
			}
            // From MFC
            // hide the window before enabling the parent, etc.
            if ( IsWindow() )
            {
                CSimpleWnd::SetWindowPos(
                    NULL, 0, 0, 0, 0,
                    SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER );
            }

			//启用上一个Domodal的窗体
			if (NULL != hWndDomodalPre)
			{
				::EnableWindow(hWndDomodalPre->m_hWnd, TRUE);
			}

            if (bEnableParent)
            {
                ::EnableWindow(hParent, TRUE);
				//::SetActiveWindow(hParent);
            }
			
			//Neko(注),这里会导致主窗体突然置后
            ::SetActiveWindow(hWndLastActive);
        }
        
        if ( IsWindow() )
            CSimpleWnd::DestroyWindow();
       
        return m_nRetCode;
    }

    void SHostDialog::EndDialog( INT_PTR nResult )
    {
		SASSERT(nResult!=RC_INIT);
		if (m_nRetCode == RC_INIT)
		{
			m_nRetCode = nResult;
			m_MsgLoop->Quit();
			CSimpleWnd::SetWindowPos(
				NULL, 0, 0, 0, 0,
				SWP_HIDEWINDOW | SWP_NOSIZE | SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER );
			PostMessage(WM_NULL);
		}
	}

	void SHostDialog::OnOK()
    {
        EndDialog(IDOK);
    }

    void SHostDialog::OnCancel()
    {
        EndDialog(IDCANCEL);
    }

	void SHostDialog::OnClose()
	{
		EndDialog(IDCLOSE);
	}

	void SHostDialog::OnDestroy()
	{
		if (m_nRetCode == RC_INIT)
		{
			EndDialog(IDCLOSE);
		}
		this->SetMsgHandled(FALSE);
	}

    void SHostDialog::OnKeyDown( UINT nChar, UINT nRepCnt, UINT nFlags )
    {
        SHostWnd::OnKeyEvent(WM_KEYDOWN,(WPARAM)nChar,MAKELPARAM(nRepCnt,nFlags));
        if(SHostWnd::IsMsgHandled()) return;
        if(nChar == VK_ESCAPE || nChar == VK_RETURN)
        {
            SWindow *pBtnExit = FindChildByID(nChar==VK_ESCAPE?IDCANCEL:IDOK);
            if(pBtnExit)
            {
                pBtnExit->FireCommand();
            }
        }
    }
	/*********************************
	描述:
		启用DoMdal栈
	参数:
		
	返回值:
		
	备注:
		
	***********************************/
	void SHostDialog::DoModalStackUse(IN bool blUse)
	{
		m_blDoModalStackUse = blUse;
	}
	/*********************************
	描述:
		更新栈中未使用的句柄
	参数:
		
	返回值:
		
	备注:
		1.去除无效窗体的句柄
	***********************************/
	void SHostDialog::DoModalStackUpdate()
	{
		SArray<SHostDialog*> doModalStackOK;
		for (size_t i = 0; i < m_doModalStack.GetCount(); i++)
		{
			SHostDialog* hWndTmp = m_doModalStack.GetAt(i);
			if (TRUE == ::IsWindow(hWndTmp->m_hWnd))
			{
				doModalStackOK.Add(hWndTmp);
			}
		}
		m_doModalStack = doModalStackOK;
	}
	/*********************************
	描述:
		添加模态窗体句柄
	参数:
		hWnd,需要添加的句柄
	返回值:
		
	备注:
		1.如果已在栈中则不添加了
	***********************************/
	void SHostDialog::DoModalStackAdd(IN SHostDialog* hWnd)
	{
		if (false == m_blDoModalStackUse)
		{
			return;
		}
		//更新
		SHostDialog::DoModalStackUpdate();
		//开始添加业务
		bool blAdd = true;
		for (size_t i = 0; i < m_doModalStack.GetCount(); i++)
		{
			SHostDialog* hWndTmp = m_doModalStack.GetAt(i);
			//表示找到了
			if (hWndTmp == hWnd)
			{
				blAdd = false;
				break;
			}
		}
		//如果可以添加才添加
		if (true == blAdd)
		{
			m_doModalStack.Add(hWnd);
		}
	}
	/*********************************
	描述:
		结束所有对话框
	参数:
		nRetSult,指定结束的标识
	返回值:
		
	备注:
		
	***********************************/
	void SHostDialog::DoModalStackEndialogAll(IN int nRetsult)
	{
		//更新
		SHostDialog::DoModalStackUpdate();
		//手动触发
		int nCount = m_doModalStack.GetCount();
		for (int i = ( nCount-1 ); i >= 0; i--)
		{
			m_doModalStack.GetAt(i)->EndDialog(nRetsult);
		}
		m_doModalStack.RemoveAll();
	}
	/*********************************
	描述:
		结束指定的对话框
	参数:
		pDlg,指定的对话框
	返回值:
		
	备注:
		
	***********************************/
	void SHostDialog::DoModalStackEndialog(IN SHostDialog* pDlg, IN int nRetsult)
	{
		//更新
		SHostDialog::DoModalStackUpdate();
		//手动触发
		int nCount = m_doModalStack.GetCount();
		for (int i = (nCount - 1); i >= 0; i--)
		{
			if (pDlg == m_doModalStack.GetAt(i))
			{
				pDlg->EndDialog(nRetsult);
				m_doModalStack.RemoveAt(i);
				break;
			}
		}
	}

	/*********************************
	描述:
		移除对应模态窗体句柄
	参数:
		hWnd,需要移除的窗体句柄
	返回值:

	备注:

	***********************************/
	void SHostDialog::DoModalStackRemove(IN SHostDialog* hWnd)
	{
		if (false == m_blDoModalStackUse)
		{
			return;
		}
		//更新
		SHostDialog::DoModalStackUpdate();
		//
		for (size_t i = 0; i < m_doModalStack.GetCount(); i++)
		{
			SHostDialog* hWndTmp = m_doModalStack.GetAt(i);
			if (hWndTmp == hWnd)
			{
				m_doModalStack.RemoveAt(i);
				break;
			}
		}
	}
	/*********************************
	描述:
		获取最后一个弹框的有效的窗体
	参数:

	返回值:
		成功返回窗体句柄,否则返回NULL
	备注:

	***********************************/
	SHostDialog* SHostDialog::DoModalStackTop()
	{
		if (false == m_blDoModalStackUse)
		{
			return NULL;
		}
		//更新
		SHostDialog::DoModalStackUpdate();
		//检测
		size_t nCount = m_doModalStack.GetCount();
		if (0 == nCount)
		{
			return NULL;
		}
		SHostDialog* hWndTarget = m_doModalStack.GetAt((nCount - 1));
		return hWndTarget;
	}
	/*********************************
	描述:
		模态弹窗顶层禁用和启用
	参数:
		blEnable,TRUE表示启用,否则FALSE
	返回值:
		成功返回操作的窗体句柄,否则返回NULL
	备注:
		
	***********************************/
	SHostDialog* SHostDialog::DoModalStackTopEnable(IN BOOL blEnable)
	{
		if (false == m_blDoModalStackUse)
		{
			return NULL;
		}
		//获取栈顶句柄
		SHostDialog* hWnd = SHostDialog::DoModalStackTop();
		if (NULL == hWnd)
		{
			return NULL;
		}
		::EnableWindow(hWnd->m_hWnd, blEnable);
		return hWnd;
	}
}
