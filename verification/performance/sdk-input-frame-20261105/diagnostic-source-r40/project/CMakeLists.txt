cmake_minimum_required(VERSION 3.24)
project(SouiMatchedBaselineInputConsumer LANGUAGES CXX)
set(CMAKE_SUPPRESS_REGENERATION ON)
# The already built, frozen R39 v5 input tool is reused without recompilation.
add_subdirectory(benchmarks/input-ui-host)
