//////////////////////////////////////////////////////////////////////////
//   File Name: SwndStyle.h
//////////////////////////////////////////////////////////////////////////

#include "souistd.h"
#include "core/Swndstyle.h"
#include "core/Swnd.h"
#include "helper/SplitString.h"
#include <algorithm>

namespace SOUI
{

SwndStyle::SwndStyle()
    : m_uAlign(Align_Center)
    , m_uVAlign(VAlign_Middle)
	, m_bMultiLines(0)
    , m_nTextAlign(0)
    , m_crBg(CR_INVALID)
    , m_crBorder(CR_INVALID)
    , m_strCursor(_T("arrow"))
    , m_byAlpha(0xFF)
    , m_bDotted(0)
    , m_bTrackMouseEvent(0)
    , m_bBkgndBlend(1)
    , m_bySepSpace(5)
	, m_nScale(100)
{
    for(int i=0; i<4; i++)
    {
        m_crText[i]=CR_INVALID;
    }
}

UINT SwndStyle::GetTextAlign()
{
    if(m_nTextAlign) return m_nTextAlign;
    UINT uRet=0;
    switch(m_uAlign)
    {
    case Align_Center: uRet |= DT_CENTER;break;
    case Align_Right: uRet|= DT_RIGHT;break;
    }
	switch (m_uVAlign)
	{
	/*=================================================*/
	//作者：jay
	//case VAlign_Middle:uRet |= DT_VCENTER | DT_SINGLELINE; break;
	//case VAlign_Bottom:uRet |= DT_BOTTOM | DT_SINGLELINE; break;
	//以上备注的代码时之前soui的方式，将单行显示和垂直居中绑定到一起。
	//这导致无法单独显示垂直居中或者单行显示。
	//现在修改了skia的SkTextLayoutEx::draw渲染，让这两个设置可以分来开设置互不影响
	case VAlign_Middle:uRet |= DT_VCENTER; break;
	case VAlign_Bottom:uRet |= DT_BOTTOM; break;
	}
	/*=================================================*/

	if (!m_bMultiLines) uRet |= DT_SINGLELINE; //作者：jay，增加多行显示的设置
	if (m_bDotted) uRet |= DT_END_ELLIPSIS;
	return uRet;
}

int SwndStyle::GetStates()
{
	int fonts = 1, colors = 1;
    for(int i=1;i<4;i++)
    {
        if(m_ftText[i].GetFontPtr()!=NULL) fonts++;
        if(m_crText[i]!=CR_INVALID) colors++;
    }
    return (std::max)(fonts,colors);
}

COLORREF SwndStyle::GetTextColor( int iState )
{
    if(m_crText[iState]==CR_INVALID) iState=0;
    return m_crText[iState];
}

IFontPtr SwndStyle::GetTextFont( int iState )
{
    if(!m_ftText[iState].GetFontPtr()) iState=0;
    return m_ftText[iState].GetFontPtr();
}

HRESULT SwndStyle::OnAttrMargin(const SStringW& strValue,BOOL bLoading)
{
	_ParseLayoutSize4(strValue, m_rcMargin);
    return !bLoading?S_OK:S_FALSE;
}

HRESULT SwndStyle::OnAttrMarginX(const SStringW& strValue,BOOL bLoading)
{
	m_rcMargin[0] = m_rcMargin[2] = GETLAYOUTSIZE(strValue);
    return !bLoading?S_OK:S_FALSE;
}

HRESULT SwndStyle::OnAttrMarginY(const SStringW& strValue,BOOL bLoading)
{
	m_rcMargin[1] = m_rcMargin[3] = GETLAYOUTSIZE(strValue);

	return !bLoading?S_OK:S_FALSE;
}

void SwndStyle::_ParseLayoutSize4(const SStringW & strValue, SLayoutSize layoutSizes[])
{
	SStringWList values;
	size_t nValues = SplitString(strValue, L',', values);
	if (nValues == 1)
	{
		layoutSizes[0] = layoutSizes[1] = layoutSizes[2] = layoutSizes[3] = GETLAYOUTSIZE(values[0]);
	}
	else if (nValues == 2)
	{
		layoutSizes[0] = GETLAYOUTSIZE(values[0]);
		layoutSizes[1] = GETLAYOUTSIZE(values[1]);
	}
	else if (nValues == 4)
	{
		layoutSizes[0] = GETLAYOUTSIZE(values[0]);
		layoutSizes[1] = GETLAYOUTSIZE(values[1]);
		layoutSizes[2] = GETLAYOUTSIZE(values[2]);
		layoutSizes[3] = GETLAYOUTSIZE(values[3]);
	}
}

void SwndStyle::_ParseLayoutSize2(const SStringW& strValue, SLayoutSize layoutSizes[])
{
	SStringWList values;
	size_t nValues = SplitString(strValue, L',', values);

	if (nValues == 1)
	{
		layoutSizes[0] = layoutSizes[1] = GETLAYOUTSIZE(values[0]);
	}
	else if (nValues == 2)
	{
		layoutSizes[0] = GETLAYOUTSIZE(values[0]);
		layoutSizes[1] = GETLAYOUTSIZE(values[1]);
	}
}

HRESULT SwndStyle::OnAttrPadding(const SStringW &strValue,BOOL bLoading)
{
	_ParseLayoutSize4(strValue, m_rcInset);
	return bLoading?S_FALSE:S_OK;
}


void SwndStyle::SetScale(int nScale)
{
	m_nScale = nScale;
	for(int i=0;i<4;i++)
	{
		m_ftText[i].SetScale(nScale);
	}
}

CRect SwndStyle::GetMargin() const
{
	CRect rcRet;
	rcRet.left = m_rcMargin[0].toPixelSize(GetScale());
	rcRet.top = m_rcMargin[1].toPixelSize(GetScale());
	rcRet.right = m_rcMargin[2].toPixelSize(GetScale());
	rcRet.bottom = m_rcMargin[3].toPixelSize(GetScale());
	return rcRet;
}

CRect SwndStyle::GetPadding() const
{
	CRect rcRet;
	rcRet.left = m_rcInset[0].toPixelSize(GetScale());
	rcRet.top = m_rcInset[1].toPixelSize(GetScale());
	rcRet.right = m_rcInset[2].toPixelSize(GetScale());
	rcRet.bottom = m_rcInset[3].toPixelSize(GetScale());
	return rcRet;

}


}//namespace SOUI