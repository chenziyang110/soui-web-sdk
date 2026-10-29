#pragma once
#include <soui/host/interaction_protocol.h>

namespace soui::sdk::detail {
[[nodiscard]] inline bool input_requires_geometry(input_kind kind) noexcept {
  return kind == input_kind::wheel || kind == input_kind::pointer_move || kind == input_kind::pointer_down;
}
// Coordinates/hit tests belong to a geometry epoch. Text and terminal edges
// belong to the retained document: resizing after admission must not erase a
// queued key-up, IME commit/end, blur, or pointer release and leave state held.
[[nodiscard]] inline bool input_matches_generation(const input_packet& packet,
    const interaction_generation& current) noexcept {
  if (packet.generation.view != current.view || packet.generation.document != current.document)
    return false;
  return input_requires_geometry(packet.kind) ? packet.generation.geometry == current.geometry :
      static_cast<std::uint32_t>(packet.kind) <= static_cast<std::uint32_t>(input_kind::ime_end);
}
} // namespace soui::sdk::detail
