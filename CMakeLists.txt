cmake_minimum_required(VERSION 3.10)
project(execsim LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if (MSVC)
  add_compile_options(/W4 /permissive-)
else()
  add_compile_options(-Wall -Wextra -Wpedantic)
endif()

# Library target
add_library(execsim execution.cpp)

add_executable(execsim_test main.cpp)

target_link_libraries(execsim_test PRIVATE execsim)