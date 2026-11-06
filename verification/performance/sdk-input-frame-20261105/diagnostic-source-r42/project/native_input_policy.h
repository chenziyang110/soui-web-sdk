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
  bool dispatch_tuple{},in_cohort{};
};
// The real WndProc and CPU contracts share this bounded policy. No fake OS
// interpretation: only tag/order/cell/window integrity and raw POD storage.
struct input_policy {
  std::array<input_row,native_capacity> rows{};
  unsigned seen{},stored{},tagged{},first_fault{};
  std::uint64_t begin{},end{},last{},max_gap{};
  bool armed{},closed{},failed{},overflow{};
  void fault(unsigned code) noexcept { failed=true;if(!first_fault)first_fault=code; }
  bool arm(std::uint64_t q) noexcept {
    if(armed||!q){fault(1);return false;}armed=true;begin=last=q;return true;
  }
  void record(input_row row) noexcept {
    row.ordinal=++seen;row.in_cohort=armed&&!closed;
    if(stored==rows.size()){overflow=true;fault(2);return;}
    rows[stored++]=row;
    if(!row.in_cohort)return;
    if(!row.receipt||row.receipt<last){fault(3);return;}
    if(row.receipt-last>max_gap)max_gap=row.receipt-last;
    last=row.receipt;
    if(!row.dispatch_tuple||!row.retrieved||row.retrieved>row.receipt){fault(4);return;}
    if(!row.tag||row.tag>input_count||row.extra!=(tag_magic|row.tag)){fault(5);return;}
    if(row.tag!=tagged+1){fault(6);return;}
    const int left=32+16*static_cast<int>((row.tag-1)%64);
    if(row.x<left||row.x>=left+16||row.y<56||row.y>=72){fault(7);return;}
    ++tagged;
  }
  void close(std::uint64_t q) noexcept {
    if(!armed||closed||!q||q<last){fault(8);return;}
    end=q;closed=true;if(q-last>max_gap)max_gap=q-last;
    if(tagged!=input_count)fault(9);
  }
  bool complete() const noexcept {
    return armed&&closed&&!failed&&!overflow&&tagged==input_count&&end>=begin;
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
