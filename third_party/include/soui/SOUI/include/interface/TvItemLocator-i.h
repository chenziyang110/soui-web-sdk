#pragma once

#include <unknown/obj-ref-i.h>
#include <unknown/obj-ref-impl.hpp>
#include "Adapter-i.h"

namespace SOUI
{
    interface ITreeViewItemLocator : public IObjRef
    {
        virtual void SetAdapter(ITvAdapter *pAdapter) PURE;//设置容器接口
        virtual void OnBranchChanged(HTREEITEM hItem) PURE;//分支改变通知
        virtual void OnBranchExpandedChanged(HTREEITEM hItem,BOOL bExpandedOld,BOOL bExpandedNew) PURE;//分支展开改变通知

        virtual int GetTotalHeight() const PURE;//获取总高度
        virtual int GetTotalWidth() const PURE;//获取总宽度
        virtual int Item2Position(HTREEITEM hItem) const PURE;//计算当前分支的所在的高度
        virtual HTREEITEM Position2Item(int position) const PURE;//
        virtual int GetScrollLineSize() const PURE;//获取滚动行的大小
        
        virtual void SetItemWidth(HTREEITEM hItem,int nWidth) PURE;//设置项宽
        virtual int GetItemWidth(HTREEITEM hItem) const PURE;//获取项宽
        virtual void SetItemHeight(HTREEITEM hItem,int nHeight) PURE;//设置项高
        virtual int GetItemHeight(HTREEITEM hItem) const PURE;//获取项高
        virtual int GetItemIndent(HTREEITEM hItem) const PURE;//获取项缩进
    };
    
}