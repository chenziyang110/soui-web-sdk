#include <soui/runtime/core/result/result.h>

#include <utility>

namespace soui::runtime {

operation_result::operation_result(result_kind result_kind,
                                   std::string result_task_id,
                                   std::string result_message)
    : kind{result_kind}, task_id{std::move(result_task_id)}, message{std::move(result_message)} {}

}  // namespace soui::runtime
