#include "SkDiscardableMemory.h"
#include "SkBitmap.h"
#include "SkBitmapCache.h"
#include "SkMipMap.h"
#include "SkResourceCache.h"
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <vector>

namespace {
int key_namespace;
struct test_key : SkResourceCache::Key {
    std::uint32_t identity;
    explicit test_key(std::uint32_t value, std::uint64_t shared=0) : identity(value) {
        init(&key_namespace, shared, sizeof(identity));
    }
};
struct record : SkResourceCache::Rec {
    test_key key;
    std::shared_ptr<std::vector<unsigned char>> data;
    int& destroyed;
    record(std::uint32_t id, std::size_t bytes, int& count, std::uint64_t shared=0)
      : key(id, shared), data(std::make_shared<std::vector<unsigned char>>(bytes, 0x5a)), destroyed(count) {}
    ~record() override { ++destroyed; }
    const Key& getKey() const override { return key; }
    std::size_t bytesUsed() const override { return sizeof(record)+data->size(); }
};
void require(bool condition, const char* message) { if(!condition)throw std::runtime_error(message); }
bool visit(const SkResourceCache::Rec&, void*) { return true; }
bool contains(SkResourceCache& cache, std::uint32_t id, std::uint64_t shared=0) {
    return cache.find(test_key(id,shared),visit,nullptr);
}
void positive_discardable_budget() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);
    require(cache.setTotalByteLimit(1024)==0,"Unexpected initial discardable byte limit");
    auto* item=new record(1,2048,destroyed);auto retained=item->data;cache.add(item);
    require(cache.getTotalBytesUsed()<1024,"Discardable cache ignored explicit retained byte limit");
    require(!contains(cache,1)&&destroyed==1,"Oversized record ownership was not released exactly once");
    require(retained->size()==2048&&retained->front()==0x5a&&retained->back()==0x5a,"External data invalidated");
}
void exact_threshold() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);cache.setTotalByteLimit(1024);
    cache.add(new record(1,100,destroyed));const auto existing=cache.getTotalBytesUsed();
    auto* exact=new record(2,1024-sizeof(record),destroyed);auto external=exact->data;cache.add(exact);
    require(!contains(cache,2)&&contains(cache,1)&&destroyed==1&&cache.getTotalBytesUsed()==existing,"Exact threshold disturbed useful cache ownership");
    require(external->front()==0x5a,"Exact-threshold eviction invalidated external data");
}
void change_limits() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);
    cache.add(new record(1,2048,destroyed));require(cache.getTotalBytesUsed()>1024,"Legacy zero mode did not retain data");
    require(cache.setTotalByteLimit(1024)==0&&cache.getTotalBytesUsed()==0&&destroyed==1,"First enabling a byte threshold did not purge");
    const auto bytes=sizeof(record)+100;
    cache.add(new record(2,100,destroyed));cache.add(new record(3,100,destroyed));
    require(cache.setTotalByteLimit(bytes+1)==1024&&cache.getTotalBytesUsed()==bytes,"Lowered threshold not applied immediately");
    require(!contains(cache,2)&&contains(cache,3),"Lowering threshold did not preserve newest entry");
    require(cache.setTotalByteLimit(4096)==bytes+1,"Raising limit returned wrong prior value");
    cache.add(new record(4,100,destroyed));require(contains(cache,3)&&contains(cache,4),"Raised limit evicted valid entries");
    require(cache.setTotalByteLimit(0)==4096,"Disabling explicit threshold returned wrong prior value");
    cache.add(new record(5,8192,destroyed));require(contains(cache,5)&&cache.getTotalBytesUsed()>4096,"Zero no longer preserves discardable legacy policy");
}
void lru_budget() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);const auto bytes=sizeof(record)+256;
    cache.setTotalByteLimit(bytes*2+1);cache.add(new record(1,256,destroyed));cache.add(new record(2,256,destroyed));
    require(contains(cache,1),"First entry missing before LRU access");cache.add(new record(3,256,destroyed));
    require(contains(cache,1)&&!contains(cache,2)&&contains(cache,3)&&destroyed==1,"Byte eviction violated LRU");
    require(cache.getTotalBytesUsed()==bytes*2,"Retained byte accounting differs after LRU eviction");
    cache.purgeAll();require(cache.getTotalBytesUsed()==0&&destroyed==3,"Purge did not release retained ownership");
}
void count_budget() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);cache.setTotalByteLimit(8*1024*1024);
    for(std::uint32_t id=0;id<1100;++id)cache.add(new record(id,1,destroyed));
    unsigned found=0;for(std::uint32_t id=0;id<1100;++id)if(contains(cache,id))++found;
    require(found==1023&&destroyed==77&&!contains(cache,0)&&contains(cache,1099),"Existing strict 1024 count threshold changed");
}
void shared_invalidation() {
    int destroyed=0;SkResourceCache cache(SkDiscardableMemory::Create);cache.setTotalByteLimit(4096);
    cache.add(new record(1,100,destroyed,10));cache.add(new record(2,100,destroyed,20));cache.add(new record(3,100,destroyed,10));
    cache.purgeSharedID(10);
    require(!contains(cache,1,10)&&contains(cache,2,20)&&!contains(cache,3,10)&&destroyed==2,"Shared-ID invalidation changed");
    require(cache.getTotalBytesUsed()==sizeof(record)+100,"Shared-ID byte accounting differs");
}
void malloc_unchanged() {
    int destroyed=0;SkResourceCache cache(static_cast<std::size_t>(1024));
    cache.add(new record(1,100,destroyed));require(contains(cache,1),"Malloc-backed entry missing");
    cache.setSingleAllocationByteLimit(4096);require(cache.getEffectiveSingleAllocationByteLimit()==1024,"Malloc single allocation clamp changed");
    cache.setTotalByteLimit(0);require(cache.getTotalBytesUsed()==0&&!contains(cache,1)&&destroyed==1,"Malloc zero threshold did not purge");
    cache.add(new record(2,100,destroyed));require(cache.getTotalBytesUsed()==0&&destroyed==2,"Malloc zero threshold retained new entry");
}
void independent_instances_and_single_allocation() {
    int destroyed=0;SkResourceCache bounded(SkDiscardableMemory::Create),legacy(SkDiscardableMemory::Create);
    bounded.setTotalByteLimit(1024);bounded.setSingleAllocationByteLimit(64*1024*1024);
    require(bounded.getEffectiveSingleAllocationByteLimit()==64*1024*1024,"Retained threshold incorrectly changed active single allocation policy");
    bounded.add(new record(1,2048,destroyed));legacy.add(new record(1,2048,destroyed));
    require(!contains(bounded,1)&&contains(legacy,1)&&legacy.getTotalByteLimit()==0,"Per-instance byte threshold leaked to another owner");
}

void real_bitmap_survives_retained_eviction() {
    SkResourceCache cache(SkDiscardableMemory::Create);cache.setTotalByteLimit(256);
    SkBitmap original,scaled;original.allocN32Pixels(32,32);original.eraseColor(SK_ColorRED);original.setImmutable();
    scaled.allocN32Pixels(16,16);scaled.eraseColor(SK_ColorGREEN);scaled.setImmutable();
    SkBitmapCache::Add(original,0.5f,0.5f,scaled,&cache);
    SkBitmap found;
    require(cache.getTotalBytesUsed()==0&&!SkBitmapCache::Find(original,0.5f,0.5f,&found,&cache),"Oversized actual bitmap retained");
    require(original.getColor(4,4)==SK_ColorRED&&scaled.getColor(4,4)==SK_ColorGREEN,"Rejected cache entry invalidated external pixels");
    cache.setTotalByteLimit(8192);SkBitmapCache::Add(original,0.5f,0.5f,scaled,&cache);
    require(SkBitmapCache::Find(original,0.5f,0.5f,&found,&cache),"Actual bitmap cache miss within byte budget");
    cache.setTotalByteLimit(1);
    require(cache.getTotalBytesUsed()==0,"Lowering retained threshold failed to evict bitmap");
    require(found.getColor(4,4)==SK_ColorGREEN&&scaled.getColor(4,4)==SK_ColorGREEN,"Eviction invalidated found/client pixel references");
}
void real_mipmap_survives_retained_rejection() {
    SkResourceCache cache(SkDiscardableMemory::Create);cache.setTotalByteLimit(1);
    SkBitmap bitmap;bitmap.allocN32Pixels(64,64);bitmap.eraseColor(SK_ColorBLUE);bitmap.setImmutable();
    const auto release=[](const SkMipMap* value){if(value)value->unref();};
    std::unique_ptr<const SkMipMap,decltype(release)> mip(SkMipMapCache::AddAndRef(bitmap,&cache),release);
    require(mip&&mip->data()&&mip->testing_only_isLocked(),"Cache rejection invalidated caller-owned mip data");
    require(!mip->testing_only_isInCache()&&cache.getTotalBytesUsed()==0,"Rejected mip remained cache-owned");
    SkMipMap::Level level{};require(mip->extractLevel(0.5f,&level)&&level.fPixels&&level.fWidth==32&&level.fHeight==32,"Rejected mip pixels are unusable");
    require(bitmap.getColor(4,4)==SK_ColorBLUE,"Mip rejection changed original bitmap");
    const SkMipMap* missing=SkMipMapCache::FindAndRef(bitmap,&cache);
    if(missing)missing->unref();require(!missing,"Rejected mip was found in cache");
}
}
int main() {
    try {
        positive_discardable_budget();exact_threshold();change_limits();lru_budget();count_budget();
        shared_invalidation();malloc_unchanged();independent_instances_and_single_allocation();
        real_bitmap_survives_retained_eviction();real_mipmap_survives_retained_rejection();
        std::cout<<"10 actual SkResourceCache byte-budget/ownership cases passed\n";return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 2;}
}
