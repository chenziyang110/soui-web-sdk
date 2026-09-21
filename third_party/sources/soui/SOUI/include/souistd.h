#include <core-def.h>

//需要IAccessible接口支持打开下面的宏: 2018.10.17
//#define SOUI_ENABLE_ACC

#ifdef DLL_CORE
# ifdef SOUI_EXPORTS
#   define SOUI_EXP __declspec(dllexport)
# else
#   define SOUI_EXP __declspec(dllimport)
# endif // SOUI_EXPORTS
#else
#   define SOUI_EXP
#endif

#define OR_API SOUI_EXP

// Change these values to use different versions
#ifndef WINVER 
#define WINVER        0x0600
#define _WIN32_WINNT    0x0600//Neko,直接改为0x600吧,不支持XP了
#endif//WINVER

#ifndef _WIN32_IE
#define _WIN32_IE    _WIN32_IE_IE70 // 原来值为 0x0601 目前我们支持的版本是win7及以上,win7对应最低版本的是ie8  Penguin 2023-06-20修改 CDlgFileSel.cpp中会使用到
#endif

#ifndef _RICHEDIT_VER
#define _RICHEDIT_VER    0x0200
#endif

#ifndef _CRT_NON_CONFORMING_SWPRINTFS
#define _CRT_NON_CONFORMING_SWPRINTFS
#endif//_CRT_NON_CONFORMING_SWPRINTFS


# pragma warning(disable:4661)
# pragma warning(disable:4251)
# pragma warning(disable:4100)    //unreferenced formal parameter
# pragma warning(disable:4355)
#ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#include <windows.h>

#include <CommCtrl.h>
#include <Shlwapi.h>
#include <OleCtl.h>
#include <tchar.h>
#include <stdio.h>

/*Neko,解决 'ATLTRACENOTIMPL': macro redefinition*/
/*jay,这个是mfc的头文件，为避免没有装mfc机子无法编译，还是先把这个备注掉了*/
//#include <atlbase.h>

#include <trace.h>
#include <utilities.h>

#include <core/SDefine.h>

#define _WTYPES_NS SOUI
#include <souicoll.h>
#include <wtl.mini/msgcrack.h>
#include <wtl.mini/souimisc.h>
#include <atl.mini/atldef.h>
#include <atl.mini/scomcli.h>
#include <string/tstring.h>
#include <string/strcpcvt.h>
#include <pugixml/pugixml.hpp>



#include <interface/render-i.h>
#include <interface/imgdecoder-i.h>

#include <soui-version.h>
#include <SApp.h>
#include <helper/SAttrCracker.h>
#include <helper/color.h>
#include <helper/slog.h>
#include <helper/slog-def.h>
#include <res.mgr/sfontpool.h>
#include <res.mgr/sresprovider.h>

#include <control/souictrls.h>
#include <control/SMessageBox.h>


#pragma comment(lib,"shlwapi.lib")
