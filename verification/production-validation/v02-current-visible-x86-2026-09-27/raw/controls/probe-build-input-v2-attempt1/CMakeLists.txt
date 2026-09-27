cmake_minimum_required(VERSION 3.20)
project(V02InputProbe LANGUAGES CXX)

add_executable(V02InputProbe V02InputProbe.cpp)
target_compile_features(V02InputProbe PRIVATE cxx_std_17)
target_compile_definitions(V02InputProbe PRIVATE UNICODE _UNICODE _WIN32_WINNT=0x0601)
target_link_libraries(V02InputProbe PRIVATE user32)
