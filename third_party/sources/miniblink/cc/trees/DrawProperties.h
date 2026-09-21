// Copyright 2012 The Chromium Authors weolar. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CC_LAYERS_DRAW_PROPERTIES_H_
#define CC_LAYERS_DRAW_PROPERTIES_H_

#include "cc/base/bdcolor.h"
#include "third_party/WebKit/Source/platform/geometry/IntRect.h"
#include "third_party/WebKit/Source/platform/geometry/FloatPoint.h"
#include "third_party/WebKit/Source/platform/geometry/FloatPoint3D.h"
#include "third_party/skia/include/utils/SkMatrix44.h"

namespace cc {

// Container for properties that layers need to compute before they can be drawn.
struct DrawProps {
    DrawProps()
    {
        screenSpaceTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        inverseScreenSpaceTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        targetSpaceTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        parentScreenSpaceTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        currentTransform = SkMatrix44(SkMatrix44::kIdentity_Constructor);
        maskLayerId = -2;
        replicaLayerId = -2;
        layerCanUseLcdText = true;
        masksToBounds = true;
        drawsContent = true;
        opaque = true;
        opacity = 1.0f; //和WebLayerImpl初始化值保持一样，否则根节点可能因为不会被updata而显示不出来。devtools里的高亮场景会出现这个问题
        backgroundColor = s_kBgColor;
        useParentBackfaceVisibility = false;
        isDoubleSided = false;
        shouldFlattenTransform = false;
    }

    void copyDrawProperties(const DrawProps& other, float otherOpacity)
    {
        screenSpaceTransform = other.screenSpaceTransform;
        inverseScreenSpaceTransform = other.inverseScreenSpaceTransform;
        targetSpaceTransform = other.targetSpaceTransform;
        parentScreenSpaceTransform = other.parentScreenSpaceTransform;
        currentTransform = other.currentTransform;
        position = other.position;
        transformOrigin = other.transformOrigin;
        shouldFlattenTransform = other.shouldFlattenTransform;
        clip = other.clip;
        opacity = otherOpacity;
    }

    void copy(const DrawProps& other)
    {
        screenSpaceTransform = other.screenSpaceTransform;
        inverseScreenSpaceTransform = other.inverseScreenSpaceTransform;
        targetSpaceTransform = other.targetSpaceTransform;
        parentScreenSpaceTransform = other.parentScreenSpaceTransform;
        currentTransform = other.currentTransform;
        position = other.position;
        transformOrigin = other.transformOrigin;
        shouldFlattenTransform = other.shouldFlattenTransform;
        clip = other.clip;
        bounds = other.bounds;
        masksToBounds = other.masksToBounds;
        drawsContent = other.drawsContent;
        opaque = other.opaque;
        opacity = other.opacity;
        maskLayerId = other.maskLayerId;
        replicaLayerId = other.replicaLayerId;
        backgroundColor = other.backgroundColor;
        useParentBackfaceVisibility = other.useParentBackfaceVisibility;
        isDoubleSided = other.isDoubleSided;
    }

    // Transforms objects from content space to screen space (viewport space).
    SkMatrix44 screenSpaceTransform;
    // Cached inverse used to map a viewport dirty rect back into layer space.
    // Computing it once during draw-property update avoids repeating a 4x4
    // inversion for every dirty region in the software and GPU raster paths.
    SkMatrix44 inverseScreenSpaceTransform;
    SkMatrix44 targetSpaceTransform;
    // Parent transform is stored explicitly so compositor animation never has
    // to recover it by inverting a possibly singular base transform.
    SkMatrix44 parentScreenSpaceTransform;
    SkMatrix44 currentTransform;
    blink::IntRect clip;

    bool layerCanUseLcdText;

    blink::IntSize bounds;
    blink::FloatPoint position;
    blink::FloatPoint3D transformOrigin;
    bool masksToBounds;
    bool drawsContent;
    bool opaque;
    float opacity;
    int maskLayerId;
    int replicaLayerId;
    SkColor backgroundColor;
    bool useParentBackfaceVisibility;
    bool isDoubleSided;
    bool shouldFlattenTransform;
};

}  // namespace cc

#endif  // CC_LAYERS_DRAW_PROPERTIES_H_
