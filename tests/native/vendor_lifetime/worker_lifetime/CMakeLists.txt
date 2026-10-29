cmake_minimum_required(VERSION 3.24)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  project(SouiWorkerLifetimeRegression LANGUAGES CXX)
  enable_testing()
endif()
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Worker lifetime regressions require Windows and MSVC")
endif()
if(NOT DEFINED SOUI_DISTRIBUTION_ROOT OR SOUI_DISTRIBUTION_ROOT STREQUAL "")
  get_filename_component(SOUI_DISTRIBUTION_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/../../../.." ABSOLUTE)
endif()
get_filename_component(WORKER_SOURCE_ROOT "${SOUI_DISTRIBUTION_ROOT}" ABSOLUTE)
set(WORKER_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
set(WORKER_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(WORKER_GENERATE ON)
include("${CMAKE_CURRENT_SOURCE_DIR}/source-bytes.cmake")
foreach(input IN LISTS worker_source_inputs worker_payload_inputs)
  set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${input}")
endforeach()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS
  "${CMAKE_CURRENT_SOURCE_DIR}/source-map.json")
add_custom_target(soui_worker_lifetime_source_bytes
  COMMAND "${CMAKE_COMMAND}" "-DWORKER_SOURCE_ROOT=${WORKER_SOURCE_ROOT}"
    "-DWORKER_TEST_DIR=${CMAKE_CURRENT_SOURCE_DIR}"
    "-DWORKER_GENERATED_DIR=${WORKER_GENERATED_DIR}" "-DWORKER_GENERATE=OFF"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/source-bytes.cmake" VERBATIM)
add_executable(soui_worker_lifetime_regressions fixture.cpp)
target_include_directories(soui_worker_lifetime_regressions PRIVATE
  "${WORKER_GENERATED_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}/stubs"
  "${CMAKE_CURRENT_SOURCE_DIR}/stubs/content/web_impl_win" "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(soui_worker_lifetime_regressions PRIVATE cxx_std_17)
target_compile_options(soui_worker_lifetime_regressions PRIVATE /EHsc /utf-8 /W4 /U_DEBUG)
target_compile_definitions(soui_worker_lifetime_regressions PRIVATE
  NDEBUG UNICODE _UNICODE NOMINMAX WINVER=0x0601 _WIN32_WINNT=0x0601)
set_property(TARGET soui_worker_lifetime_regressions PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(soui_worker_lifetime_regressions PRIVATE user32 winmm)
add_dependencies(soui_worker_lifetime_regressions soui_worker_lifetime_source_bytes)
foreach(case IN ITEMS scheduler-new-bad-alloc event-null native-launch-zero registry-reserve-bad-alloc
    startup-tls-failure ready-acquire-visibility native-exit-delayed native-wait-fails
    worker-self-delete normal-worker-close concurrent-registry-close registry-live-fence main-thread-compatibility
    main-scheduler-task-budget main-scheduler-clock-budget main-scheduler-continuation
    main-scheduler-first-task-wake main-scheduler-admission-budget main-scheduler-admission-shutdown
    worker-scheduler-task-budget
    worker-scheduler-clock-budget main-scheduler-shutdown-drain main-scheduler-pre-shutdown-drain
    scheduler-absolute-same-thread scheduler-absolute-cross-thread scheduler-absolute-late-admission
    scheduler-absolute-inputs-same-thread scheduler-absolute-inputs-cross-thread scheduler-absolute-huge-cancel
    scheduler-absolute-oom-owner scheduler-absolute-oom-producer scheduler-absolute-oom-admission
    scheduler-absolute-exit-rejection scheduler-absolute-future-shutdown)
  if(case STREQUAL "native-wait-fails" OR case STREQUAL "worker-self-delete"
      OR case STREQUAL "concurrent-registry-close" OR case STREQUAL "registry-live-fence")
    add_test(NAME kernel.native.worker-lifetime.${case} COMMAND "${CMAKE_COMMAND}"
      "-DSUBJECT=$<TARGET_FILE:soui_worker_lifetime_regressions>"
      "-DCASE_ID=${case}" "-DSUBMODE=wait"
      -P "${CMAKE_CURRENT_SOURCE_DIR}/expect-fail-fast.cmake")
  else()
    add_test(NAME kernel.native.worker-lifetime.${case} COMMAND soui_worker_lifetime_regressions "${case}")
  endif()
  set_tests_properties(kernel.native.worker-lifetime.${case} PROPERTIES TIMEOUT 15)
endforeach()
# The same native-wait-fails case also supports direct "exit-code" execution.
