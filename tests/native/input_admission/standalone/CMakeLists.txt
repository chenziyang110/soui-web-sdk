cmake_minimum_required(VERSION 3.25)
project(SouiProviderEngineContract LANGUAGES CXX)
enable_testing()
get_filename_component(soui_root "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)

# A bounded actual-source Engine check. The normal parent target still links
# Soui::Sdk; this standalone fixture does not claim to compile its Host or DLL.
add_executable(provider_engine_contract
  "${CMAKE_CURRENT_LIST_DIR}/../fixture.cpp"
  "${soui_root}/sdk/src/host/engine_thread.cpp"
  "${soui_root}/sdk/src/host/interaction_protocol.cpp"
  "${soui_root}/sdk/src/runtime/diagnostics/input_frame_trace.cpp")
target_include_directories(provider_engine_contract PRIVATE "${soui_root}/sdk/include")
target_compile_features(provider_engine_contract PRIVATE cxx_std_17)
target_compile_options(provider_engine_contract PRIVATE /EHsc /utf-8 /W4 /U_DEBUG /UNDEBUG)
target_compile_definitions(provider_engine_contract PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN WINVER=0x0601 _WIN32_WINNT=0x0601)
set_property(TARGET provider_engine_contract PROPERTY MSVC_RUNTIME_LIBRARY MultiThreadedDLL)
target_link_libraries(provider_engine_contract PRIVATE user32)
add_test(NAME provider.actual-engine-clock-abi COMMAND provider_engine_contract)
set_tests_properties(provider.actual-engine-clock-abi PROPERTIES TIMEOUT 30)
