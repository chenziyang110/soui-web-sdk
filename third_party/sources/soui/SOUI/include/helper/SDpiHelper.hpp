#pragma once
#include "SDpiScale.h"
#include <WinUser.h>
#include <wtl.mini/msgcrack.h>
#include <wtl.mini/souimisc.h>
#pragma comment(lib,"version.lib")

namespace SOUI{

	class SDpiHelper
	{
	public:
		//获取一个PE文件的version
		static BOOL PEVersion(LPCTSTR pszFileName, WORD &wMajor, WORD &wMinor, WORD & wVer3, WORD & wVer4)
		{
			DWORD dwResHandle;
			void *pBuf;
			BOOL bRet = FALSE;
			DWORD dwVerInfoSize = GetFileVersionInfoSize(pszFileName, &dwResHandle);
			if (!dwVerInfoSize) return FALSE;
			pBuf = malloc(dwVerInfoSize);
			GetFileVersionInfo(pszFileName, dwResHandle, dwVerInfoSize, pBuf);
			UINT nVersionLen;
			VS_FIXEDFILEINFO *pstFileVersion;
			if (VerQueryValue(pBuf, _T("\\"), (void**)&pstFileVersion, &nVersionLen) && nVersionLen >= sizeof(VS_FIXEDFILEINFO))
			{
				wVer4 = LOWORD(pstFileVersion->dwFileVersionLS);
				wVer3 = HIWORD(pstFileVersion->dwFileVersionLS);
				wMinor = LOWORD(pstFileVersion->dwFileVersionMS);
				wMajor = HIWORD(pstFileVersion->dwFileVersionMS);
			}
			free(pBuf);
			return TRUE;
		}


		static BOOL IsVerOrGreater(WORD wVers[4], WORD wMajor, WORD wMinor, WORD wSpBuild = 0)
		{
			if (wVers[0] < wMajor)
				return FALSE;
			if (wVers[0] > wMajor)
				return TRUE;
			if (wVers[1] < wMinor)
				return FALSE;
			if (wVers[1] > wMinor)
				return TRUE;

			return wVers[2] >= wSpBuild;
		}
		/*********************************
		描述:
			获取当前窗口句柄所在的显示器的显示比
		参数:
			hWnd,窗口句柄
		返回值:
			返回当前显示器的显示比
		备注:
			
		***********************************/
		static int getScale(HWND hWnd)
		{
			WORD wVers[4];
			PEVersion(_T("ntdll.dll"), wVers[0], wVers[1], wVers[2], wVers[3]);
			//win7
			if (!IsVerOrGreater(wVers, 6, 1, 7600))
				return 100;
			int nScale = 100;
			//For Win10 1607
			if (IsVerOrGreater(wVers, 10, 0, 14955))
			{
				HMODULE hModule = LoadLibrary(_T("User32.dll"));
				if (hModule)
				{
					typedef UINT(WINAPI *FunGetDpiForWindow)(HWND);

					FunGetDpiForWindow GetDpiForWindow = (FunGetDpiForWindow)GetProcAddress(hModule, "GetDpiForWindow");
					if (GetDpiForWindow)
					{
						UINT dpi = GetDpiForWindow(hWnd);
						nScale = dpi * 100 / 96;
					}
					FreeLibrary(hModule);
				}
			}
			//for win 8.1 or later
			else  if (IsVerOrGreater(wVers, 6, 3))
			{
				HMODULE hModule = LoadLibrary(_T("Shcore.dll"));
				if (hModule)
				{
					CRect winrc;
					GetWindowRect(hWnd, &winrc);
					HMONITOR hMonitor = MonitorFromRect(winrc, MONITOR_DEFAULTTONEAREST);
					UINT dpiX, dpiY;
					typedef enum _MONITOR_DPI_TYPE {
						MDT_EFFECTIVE_DPI = 0,
						MDT_ANGULAR_DPI = 1,
						MDT_RAW_DPI = 2,
						MDT_DEFAULT = MDT_EFFECTIVE_DPI
					} MONITOR_DPI_TYPE;
					typedef HRESULT(WINAPI *FunGetDpiForMonitor)(HMONITOR, MONITOR_DPI_TYPE, UINT*, UINT*);

					FunGetDpiForMonitor GetDpiForMonitor = (FunGetDpiForMonitor)GetProcAddress(hModule, "GetDpiForMonitor");
					if (GetDpiForMonitor)
					{
						HRESULT hr = GetDpiForMonitor(hMonitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
						if (SUCCEEDED(hr))
						{
							nScale = dpiX * 100 / 96;
						}
					}
					FreeLibrary(hModule);
				}
			}
			else
			{
				nScale = getScaleOld(hWnd);
			}
			return nScale;
		}
	
		/*********************************
		描述:
			获取窗口在主屏的显示比
		参数:
			hWnd,窗口句柄
		返回值:
			返回当前显示器的显示比
		备注:
			1.这个可以返回窗口在主屏的显示比
		***********************************/
		static int getScaleOld(HWND hWnd = NULL)
		{
			HDC screen = ::GetDC(hWnd);
			int nScale = 100;
			nScale = GetDeviceCaps(screen, LOGPIXELSX) * 100 / 96;
			ReleaseDC(hWnd, screen);
			return nScale;
		}
	};

	template <class T>
	class SDpiHandler
	{
	protected:
		void OnDpiChanged(WORD dpi, const RECT* desRect)
		{
			T *pT = static_cast<T*>(this);

			int nScale = dpi * 100 / 96;
			nScale = SDpiScale::NormalizeScale(nScale);
			HandleScaleChange(nScale, desRect);
		}
		//能过重写些函数来屏蔽一些不处理的DPI。。比如只支持 100 150 200。。。所有都支持基本不现实
		virtual void HandleScaleChange(WORD nScale, const RECT *desRect)
		{
			T *pT = static_cast<T*>(this);
			int nScaleNow = nScale;
			//nScalePre 上次的缩放比
			int nScalePre = pT->SWindow::GetScale();
			if (nScaleNow != nScalePre) //As ShostWnd::GetScale is under protect, here, we using SWindow::GetScale.
			{
				//rcPre		上次的显示区域大小
				SOUI::CRect rcPre = pT->SWindow::GetWindowRect();
				CSize sz = rcPre.Size();
				float nScaleChange = (float)nScaleNow / nScalePre;
				sz.cx = (int)(rcPre.Width() * nScaleChange);
				sz.cy = (int)(rcPre.Height() * nScaleChange);
                /**********
                作者:LSY
                描述：
                  修复由小DPI显示器变成大DPI显示器，并导致放大后的窗体尺寸超过底部栏或显示器宽度情况下，
                  在变回原先的小DPI显示器，会导致缩放回来的尺寸异常。
                举例：
				  100%DPI下窗体大小为 width=1020  height=670 ,
                  变为175%DPI下窗体大小为 width=1020*1.75=1785  height=670*1.75=1172,
                  在变回100%DPI下，尺寸变为 width: 1785*(100/175)=1020;
                  height由于大小超过底部栏因此被截短 height=1010*(100/175)=577
                备注：
                  win10不会自动恢复，win11会自动恢复成最小值
                **********/
                CSize cminSize = pT->GetHostAttr().GetMinSize(nScaleNow);
                pT->SDispatchMessage(UM_SETSCALE, nScaleNow, 0);
                pT->SetWindowPos(
                    NULL,
                    desRect->left,
                    desRect->top,
                    sz.cx < cminSize.cx ? cminSize.cx : sz.cx,
                    sz.cy < cminSize.cy ? cminSize.cy : sz.cy,
                    SWP_NOZORDER | SWP_NOACTIVATE);
			}
		}
	public:
		BOOL ProcessWindowMessage(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, LRESULT& lResult, DWORD dwMsgMapID = 0)
		{
			switch (dwMsgMapID)
			{
			case 0:
				if (uMsg == WM_INITDIALOG)
				{
					int nScale = SDpiHelper::getScale(hWnd);
					
					nScale = SDpiScale::NormalizeScale(nScale);
					CRect rc;
					::GetWindowRect(hWnd,&rc);//这里获取出来的当前主屏缩放的效果
					CSize sz = rc.Size();

					sz.cx = sz.cx * nScale / 100;
					sz.cy = sz.cy * nScale / 100;
					CPoint ntl = rc.CenterPoint();
					ntl.Offset(-sz.cx / 2, -sz.cy / 2);
					rc = CRect(ntl, sz);
					HandleScaleChange(nScale, &rc);

					lResult = 0;
				}
				if (uMsg == WM_DPICHANGED)
				{
					OnDpiChanged((WORD)HIWORD(wParam), (RECT* const)lParam);
					lResult = 0;
				}
				break;
			}
			return FALSE;
		}
	};

}
