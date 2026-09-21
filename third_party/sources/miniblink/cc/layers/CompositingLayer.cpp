
#include "cc/layers/CompositingLayer.h"

#include <algorithm>

#include "third_party/skia/include/core/SkPaint.h"
#include "third_party/skia/include/core/SkCanvas.h"

#include "third_party/WebKit/Source/platform/geometry/IntRect.h"
#include "third_party/WebKit/Source/wtf/text/WTFString.h"
#include "third_party/WebKit/Source/wtf/RefCountedLeakCounter.h"

#include "platform/image-encoders/gdiplus/GDIPlusImageEncoder.h" // TODO
#include "platform/graphics/GraphicsContext.h" // TODO
#include "platform/graphics/BitmapImage.h" // TODO
#include "platform/RuntimeEnabledFeatures.h"
#include "third_party/skia/include/core/SkTypeface.h"
#include "third_party/skia/include/core/SkBitmap.h"

#include "cc/trees/LayerTreeHost.h"
#include "cc/trees/DrawProperties.h"
#include "cc/base/MathUtil.h"
#include "cc/raster/SkBitmapRefWrap.h"
#include "cc/tiles/CompositingTile.h"
#include "cc/tiles/TileWidthHeight.h"
#include "cc/tiles/TilesAddr.h"
#include "cc/playback/TileActionInfo.h"
#include "wke/wkeGlobalVar.h"

static void transformToFlattenedSkMatrix(const SkMatrix44& transform, SkMatrix* flattened)
{
    // Convert from 4x4 to 3x3 by dropping the third row and column.
    flattened->set(0, SkMScalarToScalar(transform.get(0, 0)));
    flattened->set(1, SkMScalarToScalar(transform.get(0, 1)));
    flattened->set(2, SkMScalarToScalar(transform.get(0, 3)));
    flattened->set(3, SkMScalarToScalar(transform.get(1, 0)));
    flattened->set(4, SkMScalarToScalar(transform.get(1, 1)));
    flattened->set(5, SkMScalarToScalar(transform.get(1, 3)));
    flattened->set(6, SkMScalarToScalar(transform.get(3, 0)));
    flattened->set(7, SkMScalarToScalar(transform.get(3, 1)));
    flattened->set(8, SkMScalarToScalar(transform.get(3, 3)));
}

static void flattenCompositorTransformTo2d(SkMatrix44& matrix)
{
    matrix.set(2, 0, 0.0);
    matrix.set(2, 1, 0.0);
    matrix.set(0, 2, 0.0);
    matrix.set(1, 2, 0.0);
    matrix.set(2, 2, 1.0);
    matrix.set(3, 2, 0.0);
    matrix.set(2, 3, 0.0);
}
namespace cc {

#ifndef NDEBUG
DEFINE_DEBUG_ONLY_GLOBAL(WTF::RefCountedLeakCounter, compositingLayerCounter, ("ccCompositingLayer"));
#endif
   
CompositingLayer::CompositingLayer(int id)
{
    m_prop = new DrawProps();
    m_tilesAddr = new TilesAddr(this);
    m_numTileX = 0;
    m_numTileY = 0;
    m_parent = nullptr;
    m_id = id;
    m_hasCompositorOpacity = false;
    m_compositorOpacity = 1.0f;
    m_hasCompositorTransform = false;
    m_compositorTransform.setIdentity();
    m_hasCompositorScrollDelta = false;
    m_compositorScrollDeltaX = 0.0;
    m_compositorScrollDeltaY = 0.0;

#ifndef NDEBUG
    compositingLayerCounter.increment();
#endif
}

CompositingLayer::~CompositingLayer()
{
    delete m_prop;
    delete m_tilesAddr;

    ASSERT(!m_parent);

#ifndef NDEBUG
    compositingLayerCounter.decrement();
#endif
}

int CompositingLayer::id() const
{
    return m_id;
}

bool CompositingLayer::masksToBounds() const 
{
    return m_prop->masksToBounds;
}

bool CompositingLayer::drawsContent() const 
{
    return m_prop->drawsContent;
}

const blink::IntSize& CompositingLayer::bounds() const
{
    return m_prop->bounds;
}

float CompositingLayer::opacity() const
{
    return m_hasCompositorOpacity ? m_compositorOpacity : m_prop->opacity;
}

void CompositingLayer::setCompositorOpacity(float opacity)
{
    m_hasCompositorOpacity = true;
    m_compositorOpacity = std::max(0.0f, std::min(1.0f, opacity));
}

void CompositingLayer::clearCompositorOpacity()
{
    m_hasCompositorOpacity = false;
}

void CompositingLayer::setCompositorTransform(const SkMatrix44& transform)
{
    m_hasCompositorTransform = true;
    m_compositorTransform = transform;
}

void CompositingLayer::clearCompositorTransform()
{
    m_hasCompositorTransform = false;
    m_compositorTransform.setIdentity();
}

void CompositingLayer::setCompositorScrollDelta(double x, double y)
{
    m_hasCompositorScrollDelta = true;
    m_compositorScrollDeltaX = x;
    m_compositorScrollDeltaY = y;
}

void CompositingLayer::clearCompositorScrollDelta()
{
    m_hasCompositorScrollDelta = false;
    m_compositorScrollDeltaX = 0.0;
    m_compositorScrollDeltaY = 0.0;
}

SkMatrix44 CompositingLayer::currentTransformForCompositor() const
{
    SkMatrix44 currentTransform = m_prop->currentTransform;
    if (m_hasCompositorTransform) {
        currentTransform.setIdentity();
        const blink::FloatPoint& position = m_prop->position;
        const blink::FloatPoint3D& origin = m_prop->transformOrigin;
        if (!m_compositorTransform.isIdentity()) {
            currentTransform.preTranslate(position.x() + origin.x(),
                position.y() + origin.y(), origin.z());
            currentTransform.preConcat(m_compositorTransform);
            currentTransform.preTranslate(-origin.x(), -origin.y(), -origin.z());
        } else {
            currentTransform.preTranslate(position.x(), position.y(), 0);
        }
    }

    if (m_hasCompositorScrollDelta) {
        SkMatrix44 scrollTranslation(SkMatrix44::kIdentity_Constructor);
        scrollTranslation.preTranslate(-m_compositorScrollDeltaX,
            -m_compositorScrollDeltaY, 0.0);
        scrollTranslation.preConcat(currentTransform);
        currentTransform = scrollTranslation;
    }
    return currentTransform;
}

SkMatrix44 CompositingLayer::effectiveScreenSpaceTransform(
    const SkMatrix44* compositorParentTransform) const
{
    if (!compositorParentTransform && !m_hasCompositorTransform
        && !m_hasCompositorScrollDelta)
        return m_prop->screenSpaceTransform;

    SkMatrix44 parentTransform(SkMatrix44::kIdentity_Constructor);
    if (compositorParentTransform) {
        parentTransform = *compositorParentTransform;
    } else {
        parentTransform = m_prop->parentScreenSpaceTransform;
    }

    parentTransform.preConcat(currentTransformForCompositor());
    return parentTransform;
}

SkRect CompositingLayer::subtreeBoundsInRootSpace() const
{
    const CompositingLayer* topmostAnimatedAncestor = nullptr;
    for (const CompositingLayer* ancestor = m_parent; ancestor;
        ancestor = ancestor->m_parent) {
        if (ancestor->m_hasCompositorTransform
            || ancestor->m_hasCompositorScrollDelta)
            topmostAnimatedAncestor = ancestor;
    }

    if (!topmostAnimatedAncestor)
        return subtreeBoundsInRootSpace(nullptr);

    WTF::Vector<const CompositingLayer*> path;
    for (const CompositingLayer* ancestor = m_parent;
        ancestor && ancestor != topmostAnimatedAncestor;
        ancestor = ancestor->m_parent) {
        path.append(ancestor);
    }
    std::reverse(path.begin(), path.end());

    SkMatrix44 parentTransform =
        topmostAnimatedAncestor->effectiveScreenSpaceTransform(nullptr);
    if (topmostAnimatedAncestor->m_prop->shouldFlattenTransform)
        flattenCompositorTransformTo2d(parentTransform);

    for (size_t i = 0; i < path.size(); ++i) {
        parentTransform = path[i]->effectiveScreenSpaceTransform(&parentTransform);
        if (path[i]->m_prop->shouldFlattenTransform)
            flattenCompositorTransformTo2d(parentTransform);
    }

    return subtreeBoundsInRootSpace(&parentTransform);
}

SkRect CompositingLayer::subtreeBoundsInRootSpace(
    const SkMatrix44* compositorParentTransform) const
{
    const bool compositorTransformActive =
        compositorParentTransform || m_hasCompositorTransform
        || m_hasCompositorScrollDelta;
    const SkMatrix44 transform = effectiveScreenSpaceTransform(
        compositorParentTransform);

    SkRect result;
    result.setEmpty();

    if (m_prop->bounds.width() > 0 && m_prop->bounds.height() > 0) {
        const blink::FloatRect localBounds(0, 0,
            m_prop->bounds.width(), m_prop->bounds.height());
        const blink::FloatRect mappedBounds = MathUtil::MapClippedRect(
            transform, localBounds);
        if (!mappedBounds.isEmpty()) {
            result = SkRect::MakeXYWH(mappedBounds.x(), mappedBounds.y(),
                mappedBounds.width(), mappedBounds.height());
        }
    }

    SkMatrix44 transformForChildren = transform;
    if (m_prop->shouldFlattenTransform)
        flattenCompositorTransformTo2d(transformForChildren);
    const SkMatrix44* childParentTransform = compositorTransformActive
        ? &transformForChildren : nullptr;

    for (size_t i = 0; i < m_children.size(); ++i) {
        const SkRect childBounds =
            m_children[i]->subtreeBoundsInRootSpace(childParentTransform);
        if (childBounds.isEmpty())
            continue;
        if (result.isEmpty())
            result = childBounds;
        else
            result.join(childBounds);
    }

    return result;
}

bool CompositingLayer::opaque() const
{
    return m_prop->opaque;
}

SkColor CompositingLayer::backgroundColor() const
{
    return m_prop->backgroundColor;
}

bool CompositingLayer::isDoubleSided() const
{
    return m_prop->isDoubleSided;
}

bool CompositingLayer::useParentBackfaceVisibility() const
{
    return m_prop->useParentBackfaceVisibility;
}

void CompositingLayer::insertChild(CompositingLayer* child, size_t index)
{
    CompositingLayer* childOfImpl = static_cast<CompositingLayer*>(child);
    childOfImpl->removeFromParent();
    childOfImpl->setParent(this);

    index = std::min(index, m_children.size());
    m_children.insert(index, childOfImpl);   
}

void CompositingLayer::setParent(CompositingLayer* parent)
{
    m_parent = parent;
}

void CompositingLayer::removeAllChildren()
{
    while (m_children.size()) {
        CompositingLayer* layer = m_children[0];
        ASSERT(this == layer->parent());
        layer->removeFromParent();
    }
}

void CompositingLayer::replaceChild(CompositingLayer* reference, CompositingLayer* newLayer)
{
    ASSERT(reference);
    ASSERT(reference->parent());

    if (reference == newLayer)
        return;

    int referenceIndex = indexOfChild(reference);
    if (referenceIndex == -1) {
        RELEASE_ASSERT(false);
        return;
    }

    reference->removeFromParent();

    if (newLayer) {
        newLayer->removeFromParent();
        insertChild(newLayer, referenceIndex);
    }
}

int CompositingLayer::indexOfChild(CompositingLayer* child) const
{
    for (size_t i = 0; i < m_children.size(); ++i) {
        if (m_children[i] == child)
            return i;
    }
    return -1;
}

void CompositingLayer::removeFromParent()
{
    if (m_parent)
        m_parent->removeChildOrDependent(this);
}

void CompositingLayer::removeChildOrDependent(CompositingLayer* child)
{
    for (size_t iter = 0; iter != m_children.size(); ++iter) {
        if (m_children[iter] != child)
            continue;

        child->setParent(NULL);
        m_children.remove(iter);
        return;
    }
}

void CompositingLayer::updataDrawProp(DrawProps* prop)
{
    m_prop->copy(*prop);
}

size_t CompositingLayer::tilesSize() const
{
    return m_tilesAddr->getSize();
}

SkColor CompositingLayer::getBackgroundColor() const
{
    return m_prop->backgroundColor;
}

void CompositingLayer::updataTile(int newIndexNumX, int newIndexNumY, DrawProps* prop)
{
    TilesAddr::realloByNewXY(&m_tilesAddr, newIndexNumX, newIndexNumY);

    m_numTileX = newIndexNumX;
    m_numTileY = newIndexNumY;

    updataDrawProp(prop);
}

void CompositingLayer::cleanupUnnecessaryTile(const WTF::Vector<TileActionInfo*>& tiles)
{
    for (size_t i = 0; i < tiles.size(); ++i) {
        TileActionInfo* info = tiles[i];
        SkColor color = m_prop->backgroundColor;
        CompositingTile* tile = (CompositingTile*)m_tilesAddr->getTileByXY(info->xIndex, info->yIndex, [color] { return new CompositingTile(color); });
        ASSERT(tile == m_tilesAddr->getTileByIndex(info->index));        
        tile->clearBitmap();
        m_tilesAddr->remove(tile);
    }
}

static bool isBackFaceVisible(const SkMatrix44& matrix)
{
    // Compute whether a layer with a forward-facing normal of (0, 0, 1, 0)
    // would have its back face visible after applying the transform.
    if (matrix.isIdentity())
        return false;

    // This is done by transforming the normal and seeing if the resulting z
    // value is positive or negative. However, note that transforming a normal
    // actually requires using the inverse-transpose of the original transform.
    //
    // We can avoid inverting and transposing the matrix since we know we want
    // to transform only the specific normal vector (0, 0, 1, 0). In this case,
    // we only need the 3rd row, 3rd column of the inverse-transpose. We can
    // calculate only the 3rd row 3rd column element of the inverse, skipping
    // everything else.
    //
    // For more information, refer to:
    //   http://en.wikipedia.org/wiki/Invertible_matrix#Analytic_solution
    //

    double determinant = matrix.determinant();

    // If matrix was not invertible, then just assume back face is not visible.
    if (determinant == 0)
        return false;

    // Compute the cofactor of the 3rd row, 3rd column.
    double cofactorPart1 = matrix.get(0, 0) * matrix.get(1, 1) * matrix.get(3, 3);
    double cofactorPart2 = matrix.get(0, 1) * matrix.get(1, 3) * matrix.get(3, 0);
    double cofactorPart3 = matrix.get(0, 3) * matrix.get(1, 0) * matrix.get(3, 1);
    double cofactorPart4 = matrix.get(0, 0) * matrix.get(1, 3) * matrix.get(3, 1);
    double cofactorPart5 = matrix.get(0, 1) * matrix.get(1, 0) * matrix.get(3, 3);
    double cofactorPart6 = matrix.get(0, 3) * matrix.get(1, 1) * matrix.get(3, 0);

    double cofactor33 = cofactorPart1 + cofactorPart2 + cofactorPart3 - cofactorPart4 - cofactorPart5 - cofactorPart6;

    const SkMScalar kEpsilon = std::numeric_limits<float>::epsilon();

    // Technically the transformed z component is cofactor33 / determinant.  But
    // we can avoid the costly division because we only care about the resulting
    // +/- sign; we can check this equivalently by multiplication.
    return cofactor33 * determinant < -kEpsilon;
}

static bool isLayerBackFaceVisible(CompositingLayer* layer)
{
    // A layer with singular transform is not drawn. So, we can assume that its backface is not visible.
//     if (HasSingularTransform(layer, tree))
//         return false;
    // The current W3C spec on CSS transforms says that backface visibility should
    // be determined differently depending on whether the layer is in a "3d
    // rendering context" or not. For Chromium code, we can determine whether we
    // are in a 3d rendering context by checking if the parent preserves 3d.

//     if (LayerIsInExisting3DRenderingContext(layer))
//         return DrawTransformFromPropertyTrees(layer, tree).IsBackFaceVisible();

    // In this case, either the layer establishes a new 3d rendering context, or
    // is not in a 3d rendering context at all.
    return isBackFaceVisible(layer->drawToCanvasProperties()->currentTransform);
}

static bool layerShouldBeSkipped(CompositingLayer* layer, bool layerIsDrawn)
{
    // Layers can be skipped if any of these conditions are met.
    //   - is not drawn due to it or one of its ancestors being hidden (or having
    //     no copy requests).
    //   - does not draw content.
    //   - is transparent.
    //   - has empty bounds
    //   - the layer is not double-sided, but its back face is visible.
    //
    // Some additional conditions need to be computed at a later point after the
    // recursion is finished.
    //   - the intersection of render_surface content and layer clip_rect is empty
    //   - the visible_layer_rect is empty
    //
    // Note, if the layer should not have been drawn due to being fully
    // transparent, we would have skipped the entire subtree and never made it
    // into this function, so it is safe to omit this check here.

    if (!layerIsDrawn)
        return true;

    if (!layer->drawsContent() || layer->bounds().isEmpty())
        return true;

    CompositingLayer* backfaceTestLayer = layer;
    if (layer->useParentBackfaceVisibility()) {
        ASSERT(layer->parent());
        ASSERT(!layer->parent()->useParentBackfaceVisibility());
        if (layer)
            backfaceTestLayer = layer->parent();
    }

    // The layer should not be drawn if (1) it is not double-sided and (2) the back of the layer is known to be facing the screen.
    if (!backfaceTestLayer->isDoubleSided() && isLayerBackFaceVisible(backfaceTestLayer))
        return true;

    return false;
}

void CompositingLayer::drawDebugLine(SkCanvas& canvas, CompositingTile* tile)
{
    if (!blink::RuntimeEnabledFeatures::drawTileLineEnabled() || tile->isSolidColor())
        return;

    SkPaint paintTest;
    const SkColor color = 0xff000000 | (rand() % 3) * (rand() % 7) * GetTickCount();
    paintTest.setColor(color);
    paintTest.setStrokeWidth(4);
    paintTest.setTextSize(13);
    paintTest.setTextEncoding(SkPaint::kUTF8_TextEncoding);

    static SkTypeface* typeface = nullptr;
    if (!typeface)
        typeface = SkTypeface::RefDefault(SkTypeface::kNormal);
    paintTest.setTypeface(typeface);

    paintTest.setStrokeWidth(1);
    canvas.drawLine(0, 0, tile->postion().width(), tile->postion().height(), paintTest);
    canvas.drawLine(tile->postion().width(), 0, 0, tile->postion().height(), paintTest);

    String textTest = String::format("%d %d %d, (%d %d), (%d %d)", m_id, tile->isSolidColor(), m_children.size(), tile->xIndex(), tile->yIndex(), m_prop->bounds.width(), m_prop->bounds.height());
    CString cText = textTest.utf8();
    canvas.drawText(cText.data(), cText.length(), 5, 15, paintTest);
}

void CompositingLayer::blendToTiles(TileActionInfoVector* willRasteredTiles, const SkBitmap* bitmap, const SkRect& dirtyRect, float contentScale)
{
    const Vector<TileActionInfo*>& infos = willRasteredTiles->infos();
    for (size_t i = 0; i < infos.size(); ++i) {
        TileActionInfo* info = infos[i];
        SkColor color = m_prop->backgroundColor;
        CompositingTile* tile = (CompositingTile*)m_tilesAddr->getTileByXY(info->xIndex, info->yIndex, [color] { return new CompositingTile(color); });
        ASSERT(tile == m_tilesAddr->getTileByIndex(info->index));
        blendToTile(tile, bitmap ? bitmap : info->m_bitmap, dirtyRect, info->m_solidColor, info->m_isSolidColorCoverWholeTile, contentScale);
    } 
}

void CompositingLayer::blendToTile(CompositingTile* tile, const SkBitmap* bitmap, const SkRect& dirtyRect, SkColor* solidColor, bool isSolidColorCoverWholeTile, float contentScale)
{
    tile->allocBitmapIfNeeded(solidColor, isSolidColorCoverWholeTile);
    if (!tile->bitmap())
        return;

    if (!solidColor && !tile->bitmap()->getPixels())
        DebugBreak();

    blink::IntRect dirtyRectInTile = (blink::IntRect)dirtyRect;
    dirtyRectInTile.move(-tile->postion().x(), -tile->postion().y());
    dirtyRectInTile.intersect(blink::IntRect(0, 0, tile->postion().width(), tile->postion().height()));
    tile->eraseColor(dirtyRectInTile, nullptr);

#if 0 // debug
    String outString = String::format("RasterTask::blendToTile:%d %d, %d %d %d %d\n",
        tile->xIndex(), tile->yIndex(), dirtyRectInTile.x(), dirtyRectInTile.y(), dirtyRectInTile.width(), dirtyRectInTile.height());
    OutputDebugStringW(outString.charactersWithNullTermination().data());
#endif

    SkPaint paint;
    paint.setAntiAlias(wke::g_rendererAntiAlias);
    paint.setColor(0xFFFFFFFF);
    paint.setXfermodeMode(SkXfermode::kSrc_Mode);
    paint.setFilterQuality(kLow_SkFilterQuality);

    blink::IntRect postion = tile->postion();
    if (!postion.intersects((blink::IntRect)dirtyRect)) {
        postion.setWidth(kDefaultTileWidth);
        postion.setHeight(kDefaultTileHeight);
        if (!postion.intersects((blink::IntRect)dirtyRect))
            DebugBreak();
        return;
    }

    SkIRect dstI = (blink::IntRect)(dirtyRect);
    dstI = dstI.makeOffset(-tile->postion().x(), -tile->postion().y());

    //SkIRect srcRect = SkIRect::MakeXYWH(dstI.left() * contentScale, dstI.top() * contentScale, dstI.width() * contentScale, dstI.height() * contentScale);
    blink::IntRect srcRectI = (blink::IntRect)dirtyRect;
    srcRectI.intersect(tile->postion());
    srcRectI.move(/*tile->postion().x()*/ - dirtyRect.x(), /*tile->postion().y()*/ - dirtyRect.y());
    SkIRect srcRect = SkIRect::MakeXYWH(srcRectI.x() * contentScale, srcRectI.y() * contentScale, srcRectI.width() * contentScale, srcRectI.height() * contentScale);

    if (!tile->bitmap()->getPixels()) {
        ASSERT(solidColor);
        return;
    }

    SkCanvas canvas(*tile->bitmap());
    if (bitmap) {
        //canvas.scale(1 / contentScale, 1 / contentScale);
        canvas.drawBitmapRect(*bitmap, &srcRect, SkRect::MakeFromIRect(dirtyRectInTile), &paint);
        //canvas.drawBitmapRect(*bitmap, nullptr, dst, &paint);
    }
    drawDebugLine(canvas, tile);
}

class DoClipLayer {
public:
    static bool needClipMaskLayer(CompositingLayer* maskLayer)
    {
        if (!maskLayer || !(maskLayer->drawsContent()))
            return false;
        return 0 != maskLayer->tilesSize();
    }

    DoClipLayer(LayerTreeHost* host, CompositingLayer* layer, blink::WebCanvas* canvas, const SkRect& clip)
    {
        m_canvas = canvas;

        bool needClip = false;
        //SkRect skClipRect = clip;
        SkRect skClipRect = SkRect::MakeIWH(layer->drawToCanvasProperties()->bounds.width(), layer->drawToCanvasProperties()->bounds.height());
        if (layer->masksToBounds()) {
//             bool isIntersect = skClipRect.intersect(SkRect::MakeIWH(layer->drawToCanvasProperties()->bounds.width(), layer->drawToCanvasProperties()->bounds.height()));
//             if (!isIntersect)
//                 skClipRect.setEmpty();
            skClipRect = SkRect::MakeIWH(layer->drawToCanvasProperties()->bounds.width(), layer->drawToCanvasProperties()->bounds.height());
            needClip = true;
        }

        m_maskLayer = nullptr;
        if (-1 != layer->m_prop->maskLayerId) {
            m_maskLayer = host->getCCLayerById(layer->m_prop->maskLayerId);
            if (needClipMaskLayer(m_maskLayer)) {
                needClip = true;
                SkRect skMaskClipRect = SkRect::MakeXYWH(
                    m_maskLayer->m_prop->position.x(), 
                    m_maskLayer->m_prop->position.y(),
                    m_maskLayer->m_prop->bounds.width(), 
                    m_maskLayer->m_prop->bounds.height());
                if (!skClipRect.intersect(skMaskClipRect))
                    skClipRect.setEmpty();
                // TODO: SoftwareRenderer::DrawRenderPassQuad

//                 String output = String::format("DoClipLayer: %d\n", layer->m_prop->maskLayerId);
//                 OutputDebugStringA(output.utf8().data());
            }
        }

        if (needClip)
            canvas->clipRect(skClipRect);
    }

    ~DoClipLayer() {
    }

private:
    CompositingLayer* m_maskLayer;
    blink::WebCanvas* m_canvas;
};

class DoClipChileLayer {
public:
    DoClipChileLayer(CompositingLayer* child, blink::WebCanvas* canvas)
    {
        m_canvas = canvas;
        m_child = child;
        m_isClipChild = false;

        if (1 != child->tilesSize() && 0 != child->tilesSize()) {
            m_isClipChild = true;
            canvas->save();
            int bugFixMagicNum = child->drawToCanvasProperties()->currentTransform.isTranslate() ? 0 : 1;
            canvas->clipRect(SkRect::MakeIWH(child->drawToCanvasProperties()->bounds.width() - bugFixMagicNum, child->drawToCanvasProperties()->bounds.height() - bugFixMagicNum));
        }
    }

    void release()
    {
        if (m_isClipChild)
            m_canvas->restore();
    }

private:
    CompositingLayer* m_child;
    blink::WebCanvas* m_canvas;
    bool m_isClipChild;
};

bool CompositingLayer::drawToCanvasChildren(LayerTreeHost* host,
    SkCanvas* canvas, const SkRect& clip, float parentOpacity, int deep,
    const SkMatrix44* compositorParentTransform)
{
    bool b = false;
    for (size_t i = 0; i < children().size(); ++i) {
        CompositingLayer* child = children()[i];
        const float inheritedOpacity = parentOpacity * opacity();
        const float childOpacity = child->opacity();
        const bool drawsDescendants = !child->opaque()
            || !child->masksToBounds() || !child->drawsContent();
        const bool isolateOpacity = drawsDescendants
            && childOpacity > 0.0f && childOpacity < 1.0f;
        const bool compositorTransformActive =
            compositorParentTransform || child->hasCompositorTransform()
            || child->hasCompositorScrollDelta();
        const SkMatrix44 transformToAncestor =
            child->effectiveScreenSpaceTransform(compositorParentTransform);

        SkMatrix44 transformToAncestorInverse(
            SkMatrix44::kIdentity_Constructor);
        if (!transformToAncestor.invert(&transformToAncestorInverse))
            continue;

        SkMatrix matrixToAncestor;
        transformToFlattenedSkMatrix(transformToAncestor, &matrixToAncestor);

        canvas->save();

        SkPaint paint;
        paint.setAntiAlias(wke::g_rendererAntiAlias);
        paint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
        paint.setFilterQuality(kLow_SkFilterQuality);

        canvas->setMatrix(matrixToAncestor);

        if (isolateOpacity) {
            SkPaint groupPaint;
            groupPaint.setAlpha(static_cast<U8CPU>(
                std::ceil(childOpacity * 255.0f)));
            groupPaint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
            canvas->saveLayer(nullptr, &groupPaint);
        }

        SkRect clipInLayerdCoordinate =
            SkRect::MakeXYWH(clip.x(), clip.y(), clip.width(), clip.height());
        SkMatrix inverseMatrix;
        transformToFlattenedSkMatrix(transformToAncestorInverse, &inverseMatrix);
        inverseMatrix.mapRect(&clipInLayerdCoordinate);

        blink::IntRect clipInLayerdCoordinateInt(
            SkScalarTruncToInt(clipInLayerdCoordinate.x()),
            SkScalarTruncToInt(clipInLayerdCoordinate.y()),
            SkScalarTruncToInt(clipInLayerdCoordinate.width()),
            SkScalarTruncToInt(clipInLayerdCoordinate.height()));
        const blink::IntRect layerBounds(
            0, 0, child->bounds().width(), child->bounds().height());
        const bool contentIntersectsClip =
            layerBounds.intersects(clipInLayerdCoordinateInt);

        DoClipLayer doClipLayer(host, child, canvas, clipInLayerdCoordinate);
        DoClipChileLayer doClipChileLayer(child, canvas);
        const float childParentOpacity = isolateOpacity
            ? inheritedOpacity / childOpacity : inheritedOpacity;
        if (contentIntersectsClip)
            b |= child->drawToCanvas(host, canvas, clipInLayerdCoordinateInt,
                childParentOpacity);
        b |= host->drawCompositorScrollbarLayer(child->id(), canvas,
            clipInLayerdCoordinate);
        doClipChileLayer.release();

        SkMatrix44 transformForChildren = transformToAncestor;
        if (child->drawToCanvasProperties()->shouldFlattenTransform)
            flattenCompositorTransformTo2d(transformForChildren);
        const SkMatrix44* childParentTransform = compositorTransformActive
            ? &transformForChildren : nullptr;

        if (drawsDescendants) {
            b |= child->drawToCanvasChildren(host, canvas, clip,
                childParentOpacity,
                deep + 1, childParentTransform);
        }

        if (isolateOpacity)
            canvas->restore();

        canvas->resetMatrix();
        canvas->restore();
    }

    return b;
}

bool CompositingLayer::drawToCanvas(LayerTreeHost* host, blink::WebCanvas* canvas, const blink::IntRect& clip, float parentOpacity)
{
    U8CPU alphaVal = (int)ceil(opacity() * parentOpacity * 255);
    if (layerShouldBeSkipped(this, true) || 0 == alphaVal)
        return false;

    bool b = false;
    // The parent compositor already clips in target coordinates. Intersect
    // again in this layer's local coordinates so bitmap sampling and solid
    // fills do not process the rest of a large tile for a small animation.
    canvas->save();
    canvas->clipRect(SkRect::MakeXYWH(clip.x(), clip.y(), clip.width(), clip.height()));

    // These paint knobs are identical for every tile in this layer. Reusing
    // one stack object avoids repeating setup work for large dirty regions;
    // color and alpha remain per-tile because solid and bitmap tiles differ.
    SkPaint paint;
    paint.setAntiAlias(wke::g_rendererAntiAlias);
    paint.setXfermodeMode(SkXfermode::kSrcOver_Mode);
    paint.setFilterQuality(kLow_SkFilterQuality);

    for (TilesAddr::iterator it = m_tilesAddr->begin(); it != m_tilesAddr->end(); ++it) {
        CompositingTile* tile = (CompositingTile*)it->value;
        if (!tile->postion().intersects(clip) || !tile->bitmap())
            continue;

        blink::IntRect tilePostion = tile->postion();
        blink::IntRect dirtyTile = tilePostion;
        dirtyTile.intersect(clip);
        if (dirtyTile.isEmpty())
            continue;
        SkRect dst = (SkRect)(dirtyTile);
       
        SkColor* color = tile->getSolidColor();
        if (color) {
            paint.setColor(*color);
            paint.setAlpha((int)ceil((alphaVal * SkColorGetA(*color)) / 255.0));
            canvas->drawRect(dst, paint);
        } else {
            if (!tile->bitmap() || !tile->bitmap()->getPixels())
                DebugBreak();
            paint.setAlpha(alphaVal);
            SkIRect source = SkIRect::MakeXYWH(dirtyTile.x() - tilePostion.x(),
                                               dirtyTile.y() - tilePostion.y(),
                                               dirtyTile.width(), dirtyTile.height());
            canvas->drawBitmapRect(*tile->bitmap(), &source, dst, &paint);
        }

        b = true;
#if 0
        SkPaint paintTest;
        const SkColor color2 = 0xff000000 | (rand() % 3) * (rand() % 7) * GetTickCount();
        paintTest.setColor(color2);
        paintTest.setStrokeWidth(4);
        paintTest.setTextSize(13);
        paintTest.setTextEncoding(SkPaint::kUTF8_TextEncoding);

        static SkTypeface* typeface = nullptr;
        if (!typeface)
            typeface = SkTypeface::RefDefault(SkTypeface::kNormal);
        paintTest.setTypeface(typeface);

        paintTest.setStrokeWidth(1);
        String textTest = String::format("-----%d-------", m_id);
        CString cText = textTest.utf8();
        canvas->drawText(cText.data(), cText.length(), 5 + (int)tilePostion.x(), 15 + (int)tilePostion.y(), paintTest);
#endif
    }

    canvas->restore();

    return b;
}

//////////////////////////////////////////////////////////////////////////

CompositingImageLayer::CompositingImageLayer(int id)
	: CompositingLayer(id)
	, m_bitmap(nullptr)
{

}

CompositingImageLayer::~CompositingImageLayer()
{
    if (m_bitmap)
	    m_bitmap->deref();
}

bool CompositingImageLayer::drawToCanvas(LayerTreeHost* host, blink::WebCanvas* canvas, const blink::IntRect& clip, float parentOpacity)
{
    if (!drawsContent() || !m_bitmap || !m_bitmap->get())
        return false;

    SkPaint paint;
    U8CPU alphaVal = (int)ceil(opacity() * parentOpacity * 255);
    paint.setAlpha(alphaVal);

    const SkBitmap& bitmap = *m_bitmap->get();
    const int boundsWidth = m_prop->bounds.width();
    const int boundsHeight = m_prop->bounds.height();
    if (boundsWidth <= 0 || boundsHeight <= 0 || bitmap.width() <= 0 || bitmap.height() <= 0)
        return false;

    SkIRect dirty = SkIRect::MakeXYWH(clip.x(), clip.y(), clip.width(), clip.height());
    const SkIRect layerBounds = SkIRect::MakeWH(boundsWidth, boundsHeight);
    if (!dirty.intersect(layerBounds))
        return false;

    // Sample only the source pixels covered by this dirty region. The old
    // path submitted the whole image and relied on the canvas clip, which
    // still made large image layers pay unnecessary filtering work on CPU.
    const float scaleX = static_cast<float>(bitmap.width()) / boundsWidth;
    const float scaleY = static_cast<float>(bitmap.height()) / boundsHeight;
    SkIRect source = SkIRect::MakeLTRB(
        SkScalarFloorToInt(dirty.left() * scaleX),
        SkScalarFloorToInt(dirty.top() * scaleY),
        SkScalarCeilToInt(dirty.right() * scaleX),
        SkScalarCeilToInt(dirty.bottom() * scaleY));
    if (!source.intersect(SkIRect::MakeWH(bitmap.width(), bitmap.height())))
        return false;

    const SkRect destination = SkRect::MakeLTRB(
        static_cast<SkScalar>(dirty.left()), static_cast<SkScalar>(dirty.top()),
        static_cast<SkScalar>(dirty.right()), static_cast<SkScalar>(dirty.bottom()));
    canvas->save();
    canvas->clipRect(SkRect::MakeXYWH(clip.x(), clip.y(), clip.width(), clip.height()));
    canvas->drawBitmapRect(bitmap, &source, destination, &paint);
    canvas->restore();

    return true;
}

void CompositingImageLayer::setImage(SkBitmapRefWrap* bitmap) 
{
    if (m_bitmap)
        m_bitmap->deref();
    m_bitmap = bitmap; 
    m_bitmap->ref();
}

}

