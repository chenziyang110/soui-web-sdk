cmake_minimum_required(VERSION 3.24)
project(SouiAcceptanceThreadSamplerRegression LANGUAGES CXX)
if(NOT WIN32 OR NOT MSVC)
  message(FATAL_ERROR "Thread sampler regression requires Windows/MSVC")
endif()
set(SOUI_ACCEPTANCE_MAIN "${CMAKE_CURRENT_SOURCE_DIR}/../acceptance-app/main.cpp"
  CACHE FILEPATH "Real acceptance source directly included by the fault regression")
get_filename_component(SOUI_ACCEPTANCE_MAIN "${SOUI_ACCEPTANCE_MAIN}" ABSOLUTE
  BASE_DIR "${CMAKE_CURRENT_SOURCE_DIR}")
if(NOT EXISTS "${SOUI_ACCEPTANCE_MAIN}" OR IS_DIRECTORY "${SOUI_ACCEPTANCE_MAIN}")
  message(FATAL_ERROR "SOUI_ACCEPTANCE_MAIN must reference the real acceptance main.cpp")
endif()
find_package(SouiSdk 0.6.0 EXACT CONFIG REQUIRED)
get_target_property(sdk_imported Soui::Sdk IMPORTED)
if(NOT sdk_imported)
  message(FATAL_ERROR "Use the normal installed SDK; do not compile copied SDK TUs")
endif()
add_executable(soui_acceptance_thread_sampler_tests thread_sampler_tests.cpp)
target_compile_features(soui_acceptance_thread_sampler_tests PRIVATE cxx_std_17)
target_compile_options(soui_acceptance_thread_sampler_tests PRIVATE /W4 /EHsc /utf-8 /U_DEBUG)
target_compile_definitions(soui_acceptance_thread_sampler_tests PRIVATE
  "SOUI_ACCEPTANCE_THREAD_TEST_MAIN=\"${SOUI_ACCEPTANCE_MAIN}\"")
set_property(TARGET soui_acceptance_thread_sampler_tests PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(soui_acceptance_thread_sampler_tests PRIVATE Soui::Sdk psapi)
enable_testing()
set(thread_sampler_cases
  complete-mixed empty foreign-only minimum-owner-extent create-failure
  first-failure next-failure close-failure first-and-close-failure
  next-and-close-failure first-short first-oversized next-short next-oversized
  next-short-and-close-failure)
foreach(case IN LISTS thread_sampler_cases)
  add_test(NAME acceptance.thread-sampler.${case}
    COMMAND soui_acceptance_thread_sampler_tests ${case})
  set_tests_properties(acceptance.thread-sampler.${case} PROPERTIES TIMEOUT 30 RUN_SERIAL TRUE)
endforeach()
