#pragma once
#include <stdexcept>
#include <string>

namespace soui::benchmark {
// Consumer-only settings. They do not change the public SDK's default profile.
struct profile_options final {
  bool manual_input{false}, budget_present{false};
  unsigned budget_us{0};
  bool consume(const std::wstring& value) {
    if(value==L"--manual-input") {
      if(manual_input)throw std::invalid_argument("Duplicate manual-input option");
      manual_input=true;return true;
    }
    const std::wstring prefix=L"--engine-budget=";
    if(value.rfind(prefix,0)!=0)return false;
    if(budget_present)throw std::invalid_argument("Duplicate engine-budget option");
    const auto text=value.substr(prefix.size());
    if(text!=L"0"&&text!=L"1000"&&text!=L"2000")throw std::invalid_argument("Engine budget must be 0, 1000 or 2000");
    budget_us=text==L"1000"?1000U:text==L"2000"?2000U:0U;budget_present=true;return true;
  }
  void validate(bool diagnostic_foreground)const {
    if(budget_present&&!manual_input)throw std::invalid_argument("Explicit engine budget requires manual-input");
    if(manual_input&&diagnostic_foreground)throw std::invalid_argument("Manual input cannot request diagnostic foreground control");
  }
  template<class Setup> void cursor_setup(Setup&& setup)const {
    if(!manual_input)setup();
  }
  std::string input_json()const {
    return std::string("\"input_mode\":\"")+(manual_input?"manual":"burst-protocol")+
      "\",\"deterministic_replay\":"+(manual_input?"false":"true");
  }
};
}
