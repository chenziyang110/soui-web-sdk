#include "../../../sdk/src/host/host_frame_policy.h"
#include <limits>
#include <stdexcept>
#include <iostream>
using policy = soui::sdk::detail::host_frame_policy;
int main() {
  try {
    struct example { int target; bool capability, main, gpu, interval, compositor; int selected; };
    const example cases[] = {
      {60,true,false,false,false,false,60}, {60,true,false,false,true,false,0},
      {60,true,true,false,true,false,60}, {60,true,false,true,true,false,60},
      {60,true,false,false,false,true,0}, {60,true,true,false,false,true,60},
      {60,false,false,false,false,false,0}, {60,false,true,false,true,false,60},
      {0,true,true,false,false,false,0}
    };
    for (const auto& value : cases)
      if (policy::main_rate(value.target,value.capability,value.main,value.gpu,value.interval,value.compositor) != value.selected)
        throw std::runtime_error("explicit/legacy/main cadence precedence");
    if (!policy::fractional_cpu(true,true,false,60) || policy::fractional_cpu(true,true,false,0)
        || policy::fractional_cpu(true,false,false,60) || policy::fractional_cpu(true,true,true,60))
      throw std::runtime_error("fractional CPU requires actual positive controller");
    if (policy::rounded_interval(60) != 17 || policy::rounded_interval(144) != 7
        || policy::rounded_interval(0) != 17 || policy::rounded_interval((std::numeric_limits<int>::max)()) != 1)
      throw std::runtime_error("legacy interval rounding/default/overflow");
    std::cout << "Host frame policy SOURCE contract complete\n"; return 0;
  } catch (const std::exception& error) { std::cerr << error.what() << '\n'; return 1; }
}
