#ifndef CC_RASTER_RASTER_CANDIDATE_BUDGET_H_
#define CC_RASTER_RASTER_CANDIDATE_BUDGET_H_
#include <cstddef>
#include <cstdint>

namespace cc {
class RasterCandidateBudget {
public:
    enum class Failure : unsigned { None, LayerLimit, TileLimit, ByteLimit,
        UnknownResidentOwner, UnknownPictureOwner, FilterOwner, GenerationChanged,
        TaskLimit, ExistingWork };
    static const size_t maximumLayers = 128;
    static const size_t maximumTiles = 1024;
    static const size_t maximumTasks = 64;
    static const uint64_t maximumBytes = 64ULL * 1024 * 1024;
    bool layer() { return count(m_layers, maximumLayers, Failure::LayerLimit); }
    bool tile() { return count(m_tiles, maximumTiles, Failure::TileLimit); }
    bool task() { return count(m_tasks, maximumTasks, Failure::TaskLimit); }
    bool addBytes(uint64_t bytes)
    {
        if (bytes > maximumBytes - m_bytes)
            return reject(Failure::ByteLimit);
        m_bytes += bytes;
        return eligible();
    }
    bool reject(Failure reason) { if (m_failure == Failure::None) m_failure = reason; return false; }
    bool eligible() const { return m_failure == Failure::None; }
    Failure failure() const { return m_failure; }
    uint64_t bytes() const { return m_bytes; }
private:
    bool count(size_t& value, size_t maximum, Failure reason)
    {
        if (value == maximum)
            return reject(reason);
        ++value;
        return eligible();
    }
    uint64_t m_bytes = 0;
    size_t m_layers = 0, m_tiles = 0, m_tasks = 0;
    Failure m_failure = Failure::None;
};
} // namespace cc
#endif
