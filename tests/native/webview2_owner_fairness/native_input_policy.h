#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace owner_fairness {
constexpr unsigned input_count=3125, input_hz=125, native_capacity=8192;
constexpr std::uint64_t tag_magic=0x53400000ULL;
struct input_row {
  std::uint64_t receipt{},retrieved{},extra{},message_time{};
  std::uint32_t ordinal{},tag{},thread{},stage{};
  int x{},y{};
  bool dispatch_tuple{},in_cohort{},identity_window{};
  std::uint64_t retrieval_ordinal{},retrieval_extra{},entry_extra{};
  std::uint64_t message_hwnd{},message_wparam{},message_lparam{};
  std::uint32_t message_id{},dispatch_depth{},sent_flags{};
  int message_pt_x{},message_pt_y{};
  bool retrieval_known{},source_attempted{},source_ok{};
  std::uint32_t source_error{},source_device{},source_origin{};
};
// The real WndProc and CPU contracts share this bounded policy. No fake OS
// interpretation: only tag/order/cell/window integrity and raw POD storage.
struct input_policy {
  std::array<input_row,native_capacity> rows{};
  unsigned seen{},stored{},tagged{},positive_rows{},first_fault{};
  std::uint64_t begin{},end{},last{},max_gap{},sender_done_qpc{},sender_terminal_qpc{};
  bool armed{},closed{},failed{},overflow{},sender_done{},sender_terminal{},sequence_fault{};
  void fault(unsigned code) noexcept { failed=true;if(!first_fault)first_fault=code; }
  bool arm(std::uint64_t q) noexcept {
    if(armed||!q){fault(1);return false;}armed=true;begin=last=q;return true;
  }
  void record(input_row row) noexcept {
    row.ordinal=++seen;row.in_cohort=armed&&!closed;
    // ReceiverDrain must not shorten the original Finish observation window.
    // Extra callbacks through that terminal boundary remain strict failures.
    row.identity_window=armed&&(!closed||!sender_terminal);
    if(stored==rows.size()){overflow=true;fault(2);return;}
    rows[stored++]=row;
    if(!row.identity_window)return;
    if(!row.receipt||row.receipt<last){fault(3);return;}
    if(row.in_cohort&&row.receipt-last>max_gap)max_gap=row.receipt-last;
    last=row.receipt;
    // Nonzero InSendMessageEx is a sent-message context, not this claimed
    // queued dispatch. Retain the raw flags; they do not identify a sender.
    if(row.sent_flags)fault(14);
    if(!row.dispatch_tuple||!row.retrieval_known||!row.retrieval_ordinal||
       !row.retrieved||row.retrieved>row.receipt||row.retrieval_extra!=row.extra||row.entry_extra!=row.extra)fault(4);
    if(row.stage!=2)fault(10);
    if(!row.tag||row.tag>input_count||row.extra!=(tag_magic|row.tag)){fault(5);return;}
    // Observe the positive sequence even after a strict fault. This is a drain
    // witness only: the permanent fault latch still prevents qualification.
    ++positive_rows;
    if(sequence_fault||row.tag!=tagged+1){sequence_fault=true;fault(6);}
    else ++tagged;
    const int left=32+16*static_cast<int>((row.tag-1)%64);
    if(row.x<left||row.x>=left+16||row.y<56||row.y>=72)fault(7);
  }
  void observe_sender_done(std::uint64_t q) noexcept {
    if(sender_done)return;
    if(!armed||!q||q<begin||q<last){fault(11);return;}
    sender_done=true;sender_done_qpc=q;
  }
  void observe_sender_terminal(std::uint64_t q) noexcept {
    if(sender_terminal)return;
    if(!armed||!q||q<begin||q<last){fault(13);return;}
    sender_terminal=true;sender_terminal_qpc=q;
  }
  bool positive_sequence_complete() const noexcept {
    return !sequence_fault&&tagged==input_count&&positive_rows==input_count;
  }
  // Called only at an owner WndProc safe point, never by the monitor/fence.
  bool seal_on_owner(std::uint64_t q) noexcept {
    if(closed)return false;
    if(!sender_done||!positive_sequence_complete())return false;
    if(!armed||!q||q<last||q<sender_done_qpc){fault(8);return false;}
    end=q;closed=true;if(q-last>max_gap)max_gap=q-last;
    return true;
  }
  bool complete() const noexcept {
    return armed&&sender_done&&sender_terminal&&closed&&!failed&&!overflow&&positive_sequence_complete()&&end>=begin;
  }
};
struct progress {
  std::uint64_t count{},last{},max_gap{};
  void start(std::uint64_t q) noexcept {last=q;}
  bool note(std::uint64_t q) noexcept {
    if(!last||q<last)return false;
    if(q-last>max_gap)max_gap=q-last;last=q;++count;return true;
  }
  bool finish(std::uint64_t q) noexcept {
    if(!last||q<last)return false;
    if(q-last>max_gap)max_gap=q-last;return true;
  }
};
}
