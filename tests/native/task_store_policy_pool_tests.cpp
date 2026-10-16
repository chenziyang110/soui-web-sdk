// Source-only delivery: not compiled or run. Primitive credit controls, not a
// production heap/Bridge measurement or a production budget calibration.
#include <soui/runtime/core/task/detail/storage_allocator.h>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <utility>

using namespace soui::runtime;
namespace {
void require(bool value) { if (!value) throw std::runtime_error("Quota credit primitive contract"); }
using pool = detail::task_store_request_quota;
void controls() {
  static_assert(std::is_trivially_copyable<task_store_request_quota_snapshot>::value,
                "The quota snapshot must not own allocating storage");
  static_assert(noexcept(std::declval<const pool&>().snapshot()));
  pool unconfigured;
  const auto initial = unconfigured.snapshot();
  require(initial.status == task_store_request_quota_status::available &&
      !initial.max_live_requested_bytes && initial.owner_epoch_valid && initial.owner_epoch != 0);
  { pool::reservation pending(&unconfigured, 11);
    const auto sample = unconfigured.snapshot();
    require(sample.charged_live_requested_bytes == 11 && sample.successful_live_requested_bytes == 0);
  }
  require(unconfigured.snapshot().charged_live_requested_bytes == 0 &&
      unconfigured.snapshot().charged_peak_requested_bytes == 11 &&
      unconfigured.snapshot().successful_peak_requested_bytes == 0);
  { pool::reservation successful(&unconfigured, 7); successful.commit(); }
  require(unconfigured.snapshot().successful_live_requested_bytes == 7 &&
      unconfigured.snapshot().successful_peak_requested_bytes == 7);
  unconfigured.release(7);
  require(unconfigured.snapshot().successful_live_requested_bytes == 0 && unconfigured.valid());

  pool zero(task_store_request_quota_policy{0});
  require(zero.snapshot().max_live_requested_bytes && *zero.snapshot().max_live_requested_bytes == 0);
  bool quota = false;
  try { pool::reservation denied(&zero, 1); } catch (const task_store_request_quota_exceeded&) { quota = true; }
  require(quota && zero.snapshot().denied_requests == 1 && zero.valid() &&
      zero.snapshot().charged_live_requested_bytes == 0 && zero.snapshot().successful_live_requested_bytes == 0);

  pool exact(task_store_request_quota_policy{8});
  { pool::reservation live(&exact, 8); live.commit(); }
  quota = false;
  try { pool::reservation denied(&exact, 1); } catch (const task_store_request_quota_exceeded&) { quota = true; }
  require(quota && exact.snapshot().charged_peak_requested_bytes == 8 &&
      exact.snapshot().successful_peak_requested_bytes == 8 && exact.valid());
  exact.release(8);
  require(exact.snapshot().charged_live_requested_bytes == 0);
  require(exact.snapshot().owner_epoch != zero.snapshot().owner_epoch &&
      zero.snapshot().owner_epoch != initial.owner_epoch);

  bool length = false;
  try { (void)pool::checked_request_bytes((std::numeric_limits<std::size_t>::max)(), 2); }
  catch (const std::bad_array_new_length&) { length = true; }
  require(length && exact.snapshot().denied_requests == 1); // Not another quota denial.
  pool invalid;
  invalid.release(1);
  require(invalid.snapshot().status == task_store_request_quota_status::invalid && !invalid.valid());
  quota = false;
  try { pool::reservation rejected(&invalid, 1); } catch (const task_store_request_quota_exceeded&) { quota = true; }
  require(quota && invalid.snapshot().denied_requests == 1);
}
}
int main() { controls(); }
