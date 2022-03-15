cmake_minimum_required(VERSION 3.21)
project(PDF2Text)

set(CMAKE_CXX_STANDARD 20)

add_executable(PDF2Text main.cpp)

find_package(Boost 1.78.0 COMPONENTS program_options REQUIRED)
target_link_libraries(PDF2Text poppler-cpp)
target_include_directories(PDF2Text PRIVATE include)
