#pragma once
#ifndef SOUI_TASK_FAMILY_TEST_ONLY
#error "Private family preparation seam is available only to its regression target"
#endif

namespace soui::runtime::detail::task_family_test {
enum class prepare_point {
  registration_registry, registration_families,
  store_tasks, store_operations, store_scopes, store_receipt,
  admission_owners, admission_operations, admission_queue,
  admission_subscriptions, admission_receipt
};
void before_prepare(prepare_point point);
}  // namespace soui::runtime::detail::task_family_test
