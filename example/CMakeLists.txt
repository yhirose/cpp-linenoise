cmake_minimum_required(VERSION 3.0)
include_directories(.)
add_definitions("-std=c++1y")

add_executable(example example.cpp)
add_executable(example_old example_old.cpp)

add_executable(multiinclude
       	multiinclude/main.cpp
       	multiinclude/f1.cpp
       	multiinclude/f2.cpp
)

