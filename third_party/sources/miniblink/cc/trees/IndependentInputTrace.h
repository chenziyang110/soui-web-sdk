#ifndef CC_TREES_INDEPENDENT_INPUT_TRACE_H_
#define CC_TREES_INDEPENDENT_INPUT_TRACE_H_

#include "cc/trees/InputFrameCoverage.h"
#include "cc/input/ScrollController.h"
#include <array>

namespace cc {
// Scene authority metadata; it is independent of recorder configuration/loss.
struct SceneInputIdentity {
    uint64_t view = 0, generation = 0, document = 0, geometry = 0;
    void set(const InputFrameCoverage::Event& value)
    {
        view = value.view_id; generation = value.view_generation;
        document = value.document_generation; geometry = value.geometry_generation;
    }
    bool matches(const InputFrameCoverage::Event& value) const
    {
        return view && generation && document && geometry && view == value.view_id
            && generation == value.view_generation && document == value.document_generation
            && geometry == value.geometry_generation;
    }
};

class IndependentInputTrace {
public:
    using Event = InputFrameCoverage::Event;
    static const size_t maximumEntries = 32;
    bool append(const Event& value, const ScrollNodeKey& node, uint64_t epoch,
        uint64_t receipt, double inputTime, bool smooth)
    {
        if (!InputFrameCoverage::validRange(value) || !receipt || !epoch
            || m_size == maximumEntries)
            return false;
        Entry& entry = m_entries[m_size++];
        entry = Entry{};
        entry.event = value; entry.node = node; entry.epoch = epoch;
        entry.receipt = receipt; entry.inputTime = inputTime; entry.smooth = smooth;
        return true;
    }
    void adopt(const ScrollNodeKey& node, uint64_t epoch, uint64_t throughReceipt,
        double sampleTime, bool validVisualSample)
    {
        if (!validVisualSample)
            return;
        for (size_t index = 0; index < m_size; ++index) {
            Entry& entry = m_entries[index];
            if (entry.node == node && entry.epoch == epoch && entry.receipt <= throughReceipt
                && (!entry.smooth || sampleTime > entry.inputTime))
                entry.adopted = true;
        }
    }
    bool takeForPaint(const Event& actualFrame, Event* result)
    {
        if (!result || !actualFrame.frame_id)
            return false;
        for (size_t index = 0; index < m_size; ++index) {
            const Entry& entry = m_entries[index];
            if (!entry.adopted || !InputFrameCoverage::sameGeneration(entry.event, actualFrame))
                continue;
            *result = entry.event;
            result->frame_id = actualFrame.frame_id;
            result->task_id = 0; // Filled only from the actual executing compositor context, if known.
            result->action_first = actualFrame.action_first;
            result->action_last = actualFrame.action_last;
            result->flags = content::input_frame_trace::coverage_valid;
            result->outcome = content::input_frame_trace::terminal::none;
            result->why = content::input_frame_trace::reason::accepted;
            result->value = entry.receipt; // Compositor receipt, never the router input sequence.
            result->related_id = actualFrame.related_id; // Actual scene identity.
            remove(index);
            return true;
        }
        return false;
    }
    bool takeStale(const Event& confirmed, Event* result)
    {
        for (size_t index = 0; result && index < m_size; ++index) {
            if (!InputFrameCoverage::sameGeneration(m_entries[index].event, confirmed)) {
                *result = m_entries[index].event;
                remove(index);
                return true;
            }
        }
        return false;
    }
    bool takeNode(const ScrollNodeKey& node, uint64_t epoch, Event* result)
    {
        for (size_t index = 0; result && index < m_size; ++index) {
            if (m_entries[index].node == node && m_entries[index].epoch == epoch) {
                *result = m_entries[index].event;
                remove(index);
                return true;
            }
        }
        return false;
    }
    size_t size() const { return m_size; }
    bool takeAny(Event* result)
    {
        if (!result || !m_size)
            return false;
        *result = m_entries[--m_size].event;
        return true;
    }
private:
    struct Entry {
        Event event;
        ScrollNodeKey node;
        uint64_t epoch = 0, receipt = 0;
        double inputTime = 0;
        bool smooth = false, adopted = false;
    };
    void remove(size_t index) { m_entries[index] = m_entries[--m_size]; }
    std::array<Entry, maximumEntries> m_entries{};
    size_t m_size = 0;
};
} // namespace cc
#endif
