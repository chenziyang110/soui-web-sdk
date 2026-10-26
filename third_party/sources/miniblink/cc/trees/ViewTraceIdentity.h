#ifndef CC_TREES_VIEW_TRACE_IDENTITY_H_
#define CC_TREES_VIEW_TRACE_IDENTITY_H_

#include "cc/trees/InputFrameCoverage.h"

namespace cc {

// Confirmed owner metadata only. This object never publishes input causality.
class ViewTraceIdentity {
public:
    using Event = InputFrameCoverage::Event;
    bool set(const Event& value)
    {
        if (value.struct_size != sizeof(Event)
            || value.version != content::input_frame_trace::abi_version
            || !value.view_id || !value.view_generation
            || !value.document_generation || !value.geometry_generation
            || value.input_first || value.input_last)
            return false;
        if (m_value.view_id && (value.view_id != m_value.view_id
            || value.view_generation < m_value.view_generation
            || (value.view_generation == m_value.view_generation
                && (value.document_generation < m_value.document_generation
                    || (value.document_generation == m_value.document_generation
                        && value.geometry_generation < m_value.geometry_generation)))))
            return false;
        m_value = Event{};
        m_value.view_id = value.view_id;
        m_value.view_generation = value.view_generation;
        m_value.document_generation = value.document_generation;
        m_value.geometry_generation = value.geometry_generation;
        return true;
    }
    bool known() const { return m_value.view_id != 0; }
    const Event& value() const { return m_value; }
    bool accepts(const Event& value) const
    {
        return !known() || InputFrameCoverage::sameGeneration(m_value, value);
    }
    void retainCurrent(InputFrameCoverage& coverage, InputFrameCoverage& retired,
        size_t& lost) const
    {
        InputFrameCoverage retained;
        for (size_t index = 0; index < coverage.size(); ++index) {
            const auto& segment = coverage.at(index);
            if (accepts(segment))
                retained.append(segment); // Subset of an already bounded collector.
            else if (!retired.append(segment))
                ++lost;
        }
        coverage = retained;
    }
    Event frameContext(const Event& causal) const
    {
        Event result = known() ? m_value : Event{};
        if (causal.struct_size == sizeof(Event)
            && causal.version == content::input_frame_trace::abi_version
            && accepts(causal)) {
            if (!known()) {
                result.view_id = causal.view_id;
                result.view_generation = causal.view_generation;
                result.document_generation = causal.document_generation;
                result.geometry_generation = causal.geometry_generation;
            }
            result.input_first = causal.input_first;
            result.input_last = causal.input_last;
            result.task_id = causal.task_id;
        }
        return result;
    }
private:
    Event m_value;
};
} // namespace cc
#endif
