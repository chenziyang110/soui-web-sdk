# Host 帧策略与实际应用契约

目标 `soui_host_frame_policy_tests`，CTest `sdk.native.host-frame-policy`。可由根 native 工程或本目录独立配置；Windows MSVC 是产品工具链，纯策略/source-projection fixture 也能用 LLVM-MinGW 做两架构快验，后者不替代真实 vendor reader 编译。

本 target 覆盖现有 `main_rate` 模式优先级、公共 requested/display/backend clamp、provider size/version/capability gates、actual query/apply、attempted/applied/failed 去重、旧内核 unverified、100/200/400ms 三次有界重试、BUSY/UNAVAILABLE 重试成功、慢于100ms的 provider 失败从完成时刻开始退避，以及 generation 递增后的旧 ticket 丢弃。0 始终表示主帧控制器禁用，legacy draw interval 仍由 host 恢复，不新增 0ms 全局节流。

退避用单调时钟的合成时间验证：模拟 provider 操作耗时150ms，确认失败完成后的99ms仍不重试、100ms才允许下一次 owner 机会。它验证状态机的完成时刻语义，不测真实 provider 延迟；当前 Host 实现在 provider/query/apply 及失败读回结束后采样完成时间并传给 `complete()`，产品 Host 构建仍待验。

`generate.cjs` 从当前 vendor `WebPageImpl`、`WebPage` 与 `InputUiApi.cpp` 提取原始 ASCII 方法字节，验证两个 ABI 头逐字节相等，并写 source-binding manifest。fixture 的 owner/live-view、paint lifetime、commit publication 是显式 fake；测试能证明真实方法状态转换和 ABI 调用规则，不能证明实际 Skia、WKE 周期、GUI 或物理 FPS。

## Host 集成次序

`frame_policy_attempts` 放在 per-mount Engine-owned 私有状态中；不持有 view 或 DLL。key 使用当前 view generation、provider 指针/size/version/capabilities、mode 和稳定策略输入。进入 mount 以外的状态时 `invalidate()`。不要把同一个全局 cache 共享给多个 Host。

```cpp
const auto attempt_started = detail::frame_policy_attempts::clock::now();
const auto ticket = attempts.begin(key, attempt_started);
if (ticket) {
  // Only here query/apply; unchanged keys and bounded backoff never call them.
  // Prepare output {sizeof(output), FRAME_POLICY_ABI_VERSION, ...zero...}.
  // Query; validate output; select_frame_policy_rate(requested, display,
  //     selected_main, output.minRate, output.maxRate).
  // Apply selected effective (or skip if actual already equals it).
  // On failed apply query once to reconcile a possible partial mutation.
  // Sample after query/apply and any failure readback so the backoff starts
  // when this provider operation has actually finished.
  attempts.complete(ticket, result, attempted_rate, observed_applied,
      detail::frame_policy_attempts::clock::now());
}
```

`observed_applied=-1` 表示未取得有效快照，不表示禁用。apply OK 只有与 attempted 一致才被记为成功；失败后有实际读回仍保留失败结果。provider 错误不能通过转调无返回值 debug setter 重放。旧内核分支使用 `complete_legacy`，只能确认 attempted，不能把 configured/applied 当成功。

模式选择仍先调用 `host_frame_policy::main_rate`。仅 managed 统一路径应用显示与已确认 backend 上限并反馈给应用 pacer；显式 legacy/CPU override 导致 selected_main=0 时保持禁用，不被 fallback 强行重新打开。现有 `fractional_cpu` 必须使用可信实际状态，而不是未确认 requested。

可选 `bridge_adapter::set_backend_frame_rate_limit` 只在 Engine 发布原子限制；实际 pacer stop/start 在 UI 现有机会执行。consumer 调用公共 `clamp_frame_rate_to_limits`，然后用既有 active_frame_rate 报告真实选定 pulse。feedback-supported 和 active==applied 分别记录，老 consumer 不伪装已经同步。

## 最小验证

```text
cmake -S tests/native/host_frame_policy -B <独立构建目录>
cmake --build <独立构建目录> --config Release
ctest --test-dir <独立构建目录> -C Release --output-on-failure
```

小于最小前缀、缺能力/空指针、错误快照、永久拒绝、重试耗尽、关闭都必须保持原失败语义。真实两架构 source kernel/SDK/消费者及渲染性能由集中验证完成，不能将 source projection 通过写成产品性能验收。
