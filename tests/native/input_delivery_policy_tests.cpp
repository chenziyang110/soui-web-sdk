#include "../../sdk/src/host/input_delivery_policy.h"
#include <iostream>
#include <stdexcept>

int main() {
  using namespace soui::sdk;
  try {
    input_packet packet;
    packet.generation = {1,2,3};
    const interaction_generation resized{1,2,4};
    for (auto kind : {input_kind::key_down,input_kind::key_up,input_kind::character,input_kind::focus,
        input_kind::blur,input_kind::ime_start,input_kind::ime_update,input_kind::ime_commit,
        input_kind::ime_end,input_kind::pointer_up}) {
      packet.kind=kind;
      if (!detail::input_matches_generation(packet,resized)) throw std::runtime_error("Resize lost a retained document event");
      if (detail::input_matches_generation(packet,{1,9,4}) || detail::input_matches_generation(packet,{9,2,4}))
        throw std::runtime_error("Input crossed view/document retirement");
    }
    for (auto kind : {input_kind::pointer_move,input_kind::pointer_down,input_kind::wheel}) {
      packet.kind=kind;
      if (detail::input_matches_generation(packet,resized)) throw std::runtime_error("Stale coordinate input was delivered");
      if (!detail::input_matches_generation(packet,packet.generation)) throw std::runtime_error("Current input was rejected");
    }
    return 0;
  } catch (const std::exception& error) {std::cerr<<error.what()<<'\n';return 1;}
}
