cmake_minimum_required(VERSION 3.14...4.0)
project(linenoise LANGUAGES CXX)

add_library(linenoise INTERFACE)
target_include_directories(linenoise INTERFACE ${CMAKE_CURRENT_SOURCE_DIR})
target_compile_features(linenoise INTERFACE cxx_std_17)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  option(LINENOISE_BUILD_TESTS "Build tests" ON)
  option(LINENOISE_BUILD_EXAMPLES "Build examples" ON)

  if(LINENOISE_BUILD_TESTS)
    enable_testing()
    add_executable(linenoise-test test/test.cpp)
    target_link_libraries(linenoise-test PRIVATE linenoise)
    add_test(NAME linenoise-test COMMAND linenoise-test)
  endif()

  if(LINENOISE_BUILD_EXAMPLES)
    add_subdirectory(example)
  endif()
endif()
