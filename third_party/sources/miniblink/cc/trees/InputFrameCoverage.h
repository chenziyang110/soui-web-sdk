#ifndef CC_TREES_INPUT_FRAME_COVERAGE_H_
#define CC_TREES_INPUT_FRAME_COVERAGE_H_

#include "content/common/InputFrameTrace.h"
#include <array>
#include <limits>

namespace cc {

class InputFrameCoverage {
public:
    static const size_t maximumSegments = 32;
    using Event = content::input_frame_trace::event;
    bool append(const Event& value)
    {
        if (!validRange(value))
            return false;
        Event merged = value;
        // Revisit after every union, since one new range can connect several
        // explicitly adjacent segments. A gap is never filled by inference.
        for (size_t index = 0; index < m_count;) {
            const Event& previous = m_segments[index];
            if (sameGeneration(previous, merged) && touches(previous, merged)) {
                merged.input_first = previous.input_first < merged.input_first
                    ? previous.input_first : merged.input_first;
                merged.input_last = previous.input_last > merged.input_last
                    ? previous.input_last : merged.input_last;
                m_segments[index] = m_segments[--m_count];
                index = 0;
            } else {
                ++index;
            }
        }
        if (m_count == maximumSegments)
            return false; // Only trace coverage is lost; never input or work.
        m_segments[m_count++] = merged;
        return true;
    }
    size_t size() const { return m_count; }
    const Event& at(size_t index) const { return m_segments[index]; }
    void clear() { m_count = 0; }
    static bool sameGeneration(const Event& a, const Event& b)
    {
        return a.view_id == b.view_id && a.view_generation == b.view_generation
            && a.document_generation == b.document_generation
            && a.geometry_generation == b.geometry_generation;
    }
    static bool validRange(const Event& value)
    {
        return value.version == content::input_frame_trace::abi_version
            && value.struct_size == sizeof(Event) && value.view_id
            && value.view_generation && value.document_generation
            && value.geometry_generation && value.input_first
            && value.input_last >= value.input_first;
    }
private:
    static bool touches(const Event& a, const Event& b)
    {
        const auto maximum = (std::numeric_limits<uint64_t>::max)();
        return a.input_first <= b.input_last + (b.input_last != maximum)
            && b.input_first <= a.input_last + (a.input_last != maximum);
    }
    std::array<Event, maximumSegments> m_segments{};
    size_t m_count = 0;
};

class InputDispatchTraceStack {
public:
    using Event = InputFrameCoverage::Event;
    static const size_t maximumDepth = 16;
    bool enter(const Event& value, const Event* previous = nullptr)
    {
        if (m_exhausted)
            return false;
        if (!InputFrameCoverage::validRange(value))
            return false;
        if (m_overflowDepth || m_depth == maximumDepth) {
            if (m_overflowDepth == (std::numeric_limits<size_t>::max)()) {
                m_exhausted = true;
                return false;
            }
            ++m_overflowDepth;
            return false;
        }
        m_stack[m_depth] = value;
        m_hasPrevious[m_depth] = previous != nullptr;
        if (previous)
            m_previous[m_depth] = *previous;
        ++m_depth;
        return true;
    }
    bool leave(const Event& clear, Event* processed, Event* restore = nullptr,
        bool* hasRestore = nullptr)
    {
        if (m_exhausted)
            return false;
        if (m_overflowDepth) {
            --m_overflowDepth;
            return false;
        }
        if (!m_depth || !processed)
            return false;
        const Event value = m_stack[--m_depth];
        if (hasRestore)
            *hasRestore = m_hasPrevious[m_depth];
        if (restore && m_hasPrevious[m_depth])
            *restore = m_previous[m_depth];
        if (!InputFrameCoverage::sameGeneration(value, clear))
            return false;
        *processed = value;
        return true;
    }
    bool current(Event* value) const
    {
        if (!value || !m_depth || m_exhausted || m_overflowDepth)
            return false;
        *value = m_stack[m_depth - 1];
        return true;
    }
private:
    std::array<Event, maximumDepth> m_stack{};
    std::array<Event, maximumDepth> m_previous{};
    std::array<bool, maximumDepth> m_hasPrevious{};
    size_t m_depth = 0;
    size_t m_overflowDepth = 0;
    bool m_exhausted = false;
};
} // namespace cc
#endif
