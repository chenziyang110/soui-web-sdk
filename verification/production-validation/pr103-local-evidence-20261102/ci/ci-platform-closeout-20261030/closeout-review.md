# 历史 CI 验收文本的公开副本

此文件经过删减、脱敏和编码/换行规范化，不是原始字节。仅反映 2026-10-30 的本地静态、提交或拒绝记录；不验证新候选 e43a79，也不构成云端运行通过证据。

- 源逻辑标识：pr103-integration-evidence/ci-platform-closeout-20261030/closeout-review.md
- 源原始 SHA256：670ba290e261b644c01c2eb480a705132af5da50d4838eb1c1886661e68acb92

# PR #103 CI 收尾复核

本轮代码审查、最小环境路径隔离修复、源码清单更新与校验已完成。10个文件已暂存供清单绑定；未提交、push或触发工作流。没有运行工程CMake配置、编译产物、CTest、GUI或包内脚本。

HEAD 19d450eae04d7ee51286168712d5b60c08631e89；branch codex/ci-platform-matrix-20261030；最终索引 tree 8c052061f27ee62ae9e95b729096f31d430f8ad4。unstaged/untracked/unmerged均0，原已发布checkout干净。
完整补丁 57744 bytes，SHA256 6c6a05f3d63cdefbce2f58ba9621e4888e12848796ecbe927e6aeb00ac731f97。
清单 62652 files，SHA256 e74c73bf6ac23277fe7a9738b3e613a211ed7034962f8e0ecf9eb1af63edfa6d，blob c165c1e15c9570e28717befb2cca21f57b5ad0ec，create/verify各一次均0。增量只有7新增+2修改，无删除；releaseReady=false。

旧CI初稿patch/report已逐SHA核对未变。当前完整文件和blob身份见final-state-and-diff.json；清单增量见manifest-delta.json。

审查发现并收紧：清理CPATH/CPLUS_INCLUDE_PATH/C_INCLUDE_PATH/OBJC_INCLUDE_PATH/INCLUDE/LIB/LIBPATH/LIBRARY_PATH和PKG_CONFIG环境覆盖，防止环境补齐缺失安装依赖；未观察到实际source fallback。保留原生PATH/SDKROOT。静态负控8个及14项环境注入通过，且实际传入的parent环境对象保持不变。

两名源码审查及官方工具文档复核没有发现其他当前阻断。工作流验证的是精确PR head，不是合并后的综合提交；fork代码在一次性公开标准runner中执行，只读contents token不持久化。校验器不是任意未来PR/CMake代码的安全沙箱。

六runner官方预装工具核对：Windows2022 CMake3.31.6+VS2022；WindowsARM两份清单CMake4.4.4，新清单VS2026+ARM64组件；Ubuntu两架构CMake3.31.6+gcc/g++/make；Mac Intel通用CMake4.4.2、ARM通用4.4.3，均Clang17及Xcode/CLT。镜像可变，实际版本仍待首次CI记录。VS18 generator需CMake4.2，当前vswhere→major17/18→capabilities失败停止，无下载/回退。

Node24.19.0，官方checkout v7.0.1 / setup-node v7.1.0 完整SHA与输入已核对。Ubuntu三顶层pin与官方amd64/arm64目录一致；未运行apt解析，传递依赖/基础镜像未完全冻结。Mac producer/consumers/template均只构建本机单slice、deployment12，不表示universal2通过。

精确Runtime测试：根build sdk.portable.primitives；独立consumer build ci.runtime-installed。每Linux/Mac runner是2次CTest列表查询+2次执行，只运行2个Runtime程序；Windows 0次CTest且不执行任何工程产物。Desktop consumer/模板所有平台均不运行。

Actions permissions/workflows两个400 INVALID_ARGUMENT端点保持停止，无重新查询或更换渠道；状态未知，不推断Actions关闭。公开repo/标准runner/contentsread/无artifactcache边界未变，没有计费资源操作。〔操作指引已从公开副本省略；历史结果不变。〕


## 源报告此处的源码和操作指引附录已省略

原报告在此位置包含清单操作指引、工作流源码视图及实现代码。本公开副本未收录这些附录，不能视为原样报告。以下仅保留原报告的文件身份数据。

源码文件和派生清单身份：
```json
[
  {
    "file": ".github/workflows/platform-build.yml",
    "blob": "0649a8dac871138364f140c6a6a2c8bacfe3078e",
    "bytes": 4528,
    "sha256": "d8ea344b042c2374dd290f7b13b285e168c8c617f1bb648c10f2ce891cc76279"
  },
  {
    "file": "AGENTS.md",
    "blob": "81b53627b5cd4037014e25433edead5d70ba915b",
    "bytes": 11367,
    "sha256": "d8cba1b761121d5e5f34d40b5d29a41cc1b3b089b2458016a25c8ef6e1d7d632"
  },
  {
    "file": "docs/ci-platform-matrix.md",
    "blob": "3aa66639b72ebdb7967abc54fa1c67aee17cb6e3",
    "bytes": 11110,
    "sha256": "94d13e31fe02d43e23cb494cfd2911577121d244354fb664c6d06df7bf9e982c"
  },
  {
    "file": "docs/testing.md",
    "blob": "9d2deba9eb8c22486770846d33dac072435bafc4",
    "bytes": 34138,
    "sha256": "d0dd95b1f46b653a7f4b6ad704a3194b7443146acbd52337cf9d4dd9081f4918"
  },
  {
    "file": "source-manifest.json",
    "blob": "c165c1e15c9570e28717befb2cca21f57b5ad0ec",
    "bytes": 16822760,
    "sha256": "e74c73bf6ac23277fe7a9738b3e613a211ed7034962f8e0ecf9eb1af63edfa6d"
  },
  {
    "file": "tests/ci/installed-consumer/CMakeLists.txt",
    "blob": "9e72851e669eb20a4df800e28a215702c8b18cfb",
    "bytes": 5808,
    "sha256": "fca4c5eeb9fd154a704a948c8930013e52a05c5c6cfded86617b0086984cd644"
  },
  {
    "file": "tests/ci/installed-consumer/desktop.cpp",
    "blob": "b24176f2dba5f12df8aa9a9ccc262142d17a303d",
    "bytes": 429,
    "sha256": "228ff99236c8d5b43740e76d8763d5931aa4e4510bc8ad0f0d64243da7ef2078"
  },
  {
    "file": "tests/ci/installed-consumer/runtime.cpp",
    "blob": "168b8b244451c99b1e9d615ad2f9aa8ce6e3b8c4",
    "bytes": 2589,
    "sha256": "24177528e7a10a66851b5ddb420904a66a83a22160e9adb3e35020539c1eaad5"
  },
  {
    "file": "tools/ci/platform-build.cjs",
    "blob": "f2604602da3342eac2fceb47bb69811d9106e694",
    "bytes": 17930,
    "sha256": "92dd04aaf862a733dc2594a629c88589aee32ba1472b3c001d9cc84dd39eab86"
  },
  {
    "file": "tools/ci/validate-platform-workflow.cjs",
    "blob": "f6c402a6bf11c85a1c330213c73c1d8f867aa766",
    "bytes": 7103,
    "sha256": "4855a03c0c57fff3042331c370dec48aaf36a735dde05ace73f609859038fb26"
  }
]
```


## 原报告身份值补充索引

- 3d3c42e5aac5ba805825da76410c181273ba90b1
- 949feb2413d6458794dcd2491c4babbbce0c15c1
