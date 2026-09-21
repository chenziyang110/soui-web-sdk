// Copyright 2014 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "config.h"
#include "cc/blink/WebCompositorSupportImpl.h"
#include "cc/blink/WebLayerImpl.h"
#include "cc/blink/WebContentLayerImpl.h"
#include "cc/blink/WebScrollbarLayerImpl.h"
#include "cc/blink/WebFilterOperationsImpl.h"
#include "cc/blink/WebImageLayerImpl.h"
#include "cc/blink/WebCompositorAnimationImpl.h"

namespace cc_blink {

WebCompositorSupportImpl::WebCompositorSupportImpl() { }

WebCompositorSupportImpl::~WebCompositorSupportImpl() { }

blink::WebLayer* WebCompositorSupportImpl::createLayer() {
    return new WebLayerImpl(nullptr);
}

blink::WebContentLayer* WebCompositorSupportImpl::createContentLayer(blink::WebContentLayerClient* client)
{
    return new WebContentLayerImpl(client);
}

blink::WebScrollbarLayer* WebCompositorSupportImpl::createScrollbarLayer(blink::WebScrollbar* scrollbar, 
    blink::WebScrollbarThemePainter painter, blink::WebScrollbarThemeGeometry* themeGeometry)
{
    return new WebScrollbarLayerImpl(scrollbar, painter, themeGeometry);
}

blink::WebScrollbarLayer* WebCompositorSupportImpl::createSolidColorScrollbarLayer(blink::WebScrollbar::Orientation orientation,
    int thumbThickness, int trackStart, bool isLeftSideVerticalScrollbar)
{
    return new WebScrollbarLayerImpl(orientation, thumbThickness, trackStart, isLeftSideVerticalScrollbar);
}

blink::WebFilterOperations* WebCompositorSupportImpl::createFilterOperations()
{
    return new WebFilterOperationsImpl();
}

blink::WebImageLayer* WebCompositorSupportImpl::createImageLayer()
{
    return new WebImageLayerImpl();
}

blink::WebCompositorAnimation* WebCompositorSupportImpl::createAnimation(
    const blink::WebCompositorAnimationCurve& curve,
    blink::WebCompositorAnimation::TargetProperty targetProperty,
    int groupId, int animationId)
{
    if (targetProperty == blink::WebCompositorAnimation::TargetPropertyOpacity
        && curve.type() == blink::WebCompositorAnimationCurve::AnimationCurveTypeFloat) {
        const WebFloatAnimationCurveImpl& floatCurve =
            static_cast<const WebFloatAnimationCurveImpl&>(curve);
        return new WebCompositorAnimationImpl(floatCurve.nativeCurve(), targetProperty,
            groupId, animationId);
    }

    if (targetProperty == blink::WebCompositorAnimation::TargetPropertyTransform
        && curve.type() == blink::WebCompositorAnimationCurve::AnimationCurveTypeTransform) {
        const WebTransformAnimationCurveImpl& transformCurve =
            static_cast<const WebTransformAnimationCurveImpl&>(curve);
        if (!transformCurve.nativeCurve().isSupported())
            return nullptr;
        return new WebCompositorAnimationImpl(transformCurve.nativeCurve(), targetProperty,
            groupId, animationId);
    }

    return nullptr;
}

blink::WebFloatAnimationCurve* WebCompositorSupportImpl::createFloatAnimationCurve()
{
    return new WebFloatAnimationCurveImpl();
}

blink::WebTransformAnimationCurve*
WebCompositorSupportImpl::createTransformAnimationCurve()
{
    return new WebTransformAnimationCurveImpl();
}

blink::WebTransformOperations* WebCompositorSupportImpl::createTransformOperations()
{
    return new WebTransformOperationsImpl();
}

} // cc_blink
