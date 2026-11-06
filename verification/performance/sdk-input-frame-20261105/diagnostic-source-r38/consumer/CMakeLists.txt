cmake_minimum_required(VERSION 3.24)
project(SouiSyncInputWitnessRecovery LANGUAGES CXX)
set(CMAKE_SUPPRESS_REGENERATION ON)
# The already built, frozen e54c... input tool is reused without recompilation.
add_subdirectory(benchmarks/input-ui-host)
