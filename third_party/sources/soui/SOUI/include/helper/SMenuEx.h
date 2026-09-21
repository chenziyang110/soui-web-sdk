#pragma once

#include <core/shostwnd.h>

/*=================================================================
 * @brief
 *      添加加载外部HICON图标方法
 * @author
 *      LSY
 * @date
 *      2024/06/18
 * @remark
 *      1. <menuItem>新增以下属性
 *           - autoIcon="1"   1表示开启，0表示不开启外部传入HICON
 *		2. SMenuExItem:新增以下函数添加子项内容
 * 	         -  void SetIcon(HICON hIcon , SIZE size);  autoIcon=1 才有效
        3. SMenuExItem::OnPaint 加入绘制HICON方法

**=================================================================*/
namespace SOUI
{
	class SMenuExRoot;
    class SMenuEx;
    class SOUI_EXP SMenuExItem : public SWindow
    {
        SOUI_CLASS_NAME(SMenuExItem,L"menuItem")
    public:
        SMenuExItem(SMenuEx *pOwnerMenu,ISkinObj *pItemSkin);

        ~SMenuExItem();

        SMenuEx * GetSubMenu();

        SMenuEx * GetOwnerMenu();

        void HideSubMenu();

        void ShowSubMenu(BOOL bCheckFirstItem);

        WCHAR GetHotKey() const;

        void OnSubMenuHided(BOOL bUncheckItem);
        /*
           2024/6/17 lsy 增加菜单栏动态加载外部传入的HICON方法
           HICON 外部传入的HICON图标数据
           SIZE 图标的大小
        */
        void SetIcon(HICON hIcon , SIZE size);
    protected:
        virtual BOOL CreateChildren(pugi::xml_node xmlNode);

        virtual CSize GetDesiredSize(LPCRECT pRcContainer);

        virtual void GetTextRect(LPRECT pRect);


    protected:

        BOOL OnEraseBkgnd(IRenderTarget *pRT);

        void OnPaint(IRenderTarget *pRT);

        SOUI_MSG_MAP_BEGIN()
            MSG_WM_ERASEBKGND_EX(OnEraseBkgnd)
            MSG_WM_PAINT_EX(OnPaint)
        SOUI_MSG_MAP_END()

    protected:
        SOUI_ATTRS_BEGIN()
            ATTR_INT(L"icon",m_iIcon,TRUE)
            ATTR_INT(L"check",m_bCheck,TRUE)
            ATTR_INT(L"radio", m_bRadio,TRUE)
            ATTR_INT(L"autoIcon", m_autoIcon, TRUE)
            ATTR_CHAR(L"hotKey",m_cHotKey,FALSE)
        SOUI_ATTRS_END()

        SMenuEx * m_pSubMenu;
        SMenuEx * m_pOwnerMenu;
        int       m_iIcon;
        BOOL      m_bCheck;
        BOOL      m_bRadio;    
        BOOL      m_autoIcon;
        WCHAR     m_cHotKey;
        HICON m_theIcon; /**< 图标资源句柄 */
        SIZE m_iconSize;
    };

    class SOUI_EXP SMenuEx : public SHostWnd
    {
        friend class SMenuExItem;
        friend class SMenuExRunData;
    public:
        SMenuEx(void);
        virtual ~SMenuEx(void);

        BOOL LoadMenu(LPCTSTR pszMenu,HWND hParent=NULL);
        BOOL LoadMenu(pugi::xml_node xmlNode,HWND hParent=NULL);
		UINT TrackPopupMenu(UINT flag,int x,int y,HWND hOwner,int nScale = 100);
		static void ExitPopupMenu(int nCmdId=0);

        SMenuExItem * GetParentItem() {return m_pParent;}
        SMenuEx * GetSubMenu(int nID);
        SMenuExItem * GetMenuItem(int nID);

		DWORD GetContextHelpId() const;

		void SetContextHelpId(DWORD dwId);

		HWND GetParentWnd() {return m_hParent;}
		/**
		* InsertMenu
		* @brief    插入菜单项
		* @param uPos -- 插入位置，-1代表append
		* @param uFlag -- uFlag, MF_BYPOSITION|MF_BYCOMMAND
		* @param nId -- menu id
		* @param lpNewItem -- menu string
		* @return   BOOL, true or false
		*
		* Describe
		*/
		BOOL InsertMenu(UINT uPos,UINT uFlag,int nId,LPCTSTR lpNewItem);
		
		BOOL DeleteMenu(UINT uPos, UINT uFlag);

		BOOL CheckMenuItem(UINT uPos, UINT uFlag);

		BOOL CheckMenuRadioltem(UINT idFirst, UINT idLast, UINT idCheck, UINT uFlags);
    protected:
		//创建一个空菜单,不应该在外部调用
		BOOL IniNullMenu(SMenuExRoot *ParentRoot);
        int OnMouseActivate(HWND wndTopLevel, UINT nHitTest, UINT message);
        void OnTimer(UINT_PTR timeID);
        void OnKeyDown(UINT nChar, UINT nRepCnt, UINT nFlags);

        BEGIN_MSG_MAP_EX(SMenuEx)
            MSG_WM_MOUSEACTIVATE(OnMouseActivate)
            MSG_WM_TIMER(OnTimer)
            MSG_WM_KEYDOWN(OnKeyDown)
            CHAIN_MSG_MAP(SHostWnd)
        END_MSG_MAP()
    protected:
        virtual BOOL _HandleEvent(EventArgs *pEvt);
		virtual const SStringW & GetTranslatorContext()const;
		virtual int GetScale() const;

		SWindow * FindItem(UINT uPos, UINT uFlag);

        void ShowMenu(UINT uFlag,int x,int y);
        void HideMenu(BOOL bUncheckParentItem);
        void HideSubMenu();
        void RunMenu(HWND hOwner);

        void PopupSubMenu(SMenuExItem * pItem,BOOL bCheckFirstItem);
        void OnSubMenuHided(BOOL bUncheckItem);

        SMenuEx(SMenuExItem *pParent);
		void SendInitPopupMenu2Owner(int idx);
		SMenuExItem * m_pParent;
        SMenuExItem * m_pHoverItem;
        SMenuExItem * m_pCheckItem;

		BOOL	m_bMenuInitialized;
		HWND		m_hParent;

    };

}
