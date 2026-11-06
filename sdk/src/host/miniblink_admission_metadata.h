#pragma once

#include "miniblink_request_route.h"

#include <cstdint>
#include <string>
#include <string_view>

namespace soui::sdk::detail {

// Synchronous view of this native call's already-parsed admission fields.
// The three strings are distinct, live caller-owned objects. The field strings
// remain unchanged until admission returns; they must not borrow request's bytes.
// This object neither parses nor authorizes. Do not capture it in queued work.
struct miniblink_admission_metadata final {
  miniblink_admission_metadata(std::string& request, const std::string& request_id,
      const std::string& method, std::uint32_t timeout_ms) noexcept
      : request_(&request), request_id_(request_id), method_(method),
        timeout_ms_(timeout_ms), route_(classify_miniblink_request_route(method)) {}

  miniblink_admission_metadata(std::string&, const std::string&&,
      const std::string&, std::uint32_t) = delete;
  miniblink_admission_metadata(std::string&, const std::string&,
      const std::string&&, std::uint32_t) = delete;
  miniblink_admission_metadata(const miniblink_admission_metadata&) = delete;
  miniblink_admission_metadata& operator=(const miniblink_admission_metadata&) = delete;
  miniblink_admission_metadata(miniblink_admission_metadata&&) = delete;
  miniblink_admission_metadata& operator=(miniblink_admission_metadata&&) = delete;

  // The private native-admission shim obtains its sole raw argument here, then
  // moves it into the existing owned request. The field views remain independent.
  [[nodiscard]] std::string& request() const noexcept { return *request_; }
  [[nodiscard]] std::string_view request_id() const noexcept { return request_id_; }
  [[nodiscard]] std::string_view method() const noexcept { return method_; }
  [[nodiscard]] std::uint32_t timeout_ms() const noexcept { return timeout_ms_; }
  [[nodiscard]] miniblink_request_route route() const noexcept { return route_; }

 private:
  std::string* const request_;
  const std::string_view request_id_;
  const std::string_view method_;
  const std::uint32_t timeout_ms_;
  const miniblink_request_route route_;
};

} // namespace soui::sdk::detail
