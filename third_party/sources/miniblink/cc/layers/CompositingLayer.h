#ifndef cc_LayerForCompositing_h
#define cc_LayerForCompositing_h

#include "third_party/WebKit/Source/wtf/Vector.h"
#include "third_party/WebKit/public/platform/WebCanvas.h"
#include "third_party/skia/include/core/SkColor.h"
#include "third_party/skia/include/utils/SkMatrix44.h"

class SkBitmap;
struct SkRect;

namespace blink {
class IntRect;
class IntSize;
}

namespace cc {

struct DrawProps;
class CompositingTile;
struct TileActionInfo;
class TileActionInfoVector;
class SkBitmapRefWrap;
class LayerTreeHost;
class TilesAddr;

class CompositingLayer {
public:
    CompositingLayer(int id);
    virtual ~CompositingLayer();

    enum CCType {
        NormalCCLayer,
        ImageCCLayer,
    };
    virtual CCType ccType() const { return NormalCCLayer; }

    int id() const;

    void insertChild(CompositingLayer* child, size_t index);
    void removeFromParent();

    void setParent(CompositingLayer* layer);
    CompositingLayer* parent() const { return m_parent; }
    void replaceChild(CompositingLayer* reference, CompositingLayer* newLayer);
    int indexOfChild(CompositingLayer* child) const;
    void removeChildOrDependent(CompositingLayer* child);
    void removeAllChildren();

    WTF::Vector<CompositingLayer*>& children() { return m_children; }

    DrawProps* drawToCanvasProperties() { return m_prop; }
    void updataDrawProp(DrawProps* m_prop);

    bool masksToBounds() const;
    bool drawsContent() const;
    const blink::IntSize& bounds() const;
    bool opaque() const;
    float opacity() const;
    void setCompositorOpacity(float opacity);
    void clearCompositorOpacity();
    bool hasCompositorOpacity() const { return m_hasCompositorOpacity; }
    void setCompositorTransform(const SkMatrix44& transform);
    void clearCompositorTransform();
    bool hasCompositorTransform() const { return m_hasCompositorTransform; }
    void setCompositorScrollDelta(double x, double y);
    void clearCompositorScrollDelta();
    bool hasCompositorScrollDelta() const { return m_hasCompositorScrollDelta; }
    SkRect subtreeBoundsInRootSpace() const;
    SkColor backgroundColor() const;
    bool isDoubleSided() const;
    bool useParentBackfaceVisibility() const;

    void updataTile(int newIndexNumX, int newIndexNumY, DrawProps* prop);
    void cleanupUnnecessaryTile(const WTF::Vector<TileActionInfo*>& tiles);

    virtual bool drawToCanvas(LayerTreeHost* host, blink::WebCanvas* canvas, const blink::IntRect& clip, float parentOpacity);

    void blendToTiles(TileActionInfoVector* willRasteredTiles, const SkBitmap* bitmap, const SkRect& dirtyRect, float contentScale);
    void blendToTilesByTiles(TileActionInfoVector* willRasteredTiles);
    
    bool drawToCanvasChildren(LayerTreeHost* host, SkCanvas* canvas, const SkRect& clip, float parentOpacity, int deep, const SkMatrix44* compositorParentTransform = nullptr);

    size_t tilesSize() const;

    SkColor getBackgroundColor() const;

protected:
    friend class DoClipLayer;
    void blendToTile(CompositingTile* tile, const SkBitmap* bitmap, const SkRect& dirtyRect, SkColor* solidColor, bool isSolidColorCoverWholeTile, float contentScale);

    void drawDebugLine(SkCanvas& canvas, CompositingTile* tile);
    SkMatrix44 currentTransformForCompositor() const;
    SkMatrix44 effectiveScreenSpaceTransform(const SkMatrix44* compositorParentTransform) const;
    SkRect subtreeBoundsInRootSpace(const SkMatrix44* compositorParentTransform) const;
        
    int m_id;
    DrawProps* m_prop;

    TilesAddr* m_tilesAddr;
    int m_numTileX;
    int m_numTileY;

    WTF::Vector<CompositingLayer*> m_children;
    CompositingLayer* m_parent;
    bool m_hasCompositorOpacity;
    float m_compositorOpacity;
    bool m_hasCompositorTransform;
    SkMatrix44 m_compositorTransform;
    bool m_hasCompositorScrollDelta;
    double m_compositorScrollDeltaX;
    double m_compositorScrollDeltaY;
};

class CompositingImageLayer : public CompositingLayer {
public:
    CompositingImageLayer(int id);
    virtual ~CompositingImageLayer() override;
    virtual bool drawToCanvas(LayerTreeHost* host, blink::WebCanvas* canvas, const blink::IntRect& clip, float parentOpacity) override;
    void setImage(SkBitmapRefWrap* bitmap);
    virtual CCType ccType() const override { return CompositingLayer::ImageCCLayer;    }
    
private:
    SkBitmapRefWrap* m_bitmap;
};

}

#endif // cc_LayerForCompositing_
