cmake_minimum_required(VERSION 3.16)
project(interciclo_vision LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(OpenCV REQUIRED COMPONENTS
    core imgproc highgui imgcodecs videoio viz
)

add_executable(interciclo_vision Principal.cpp)

target_include_directories(interciclo_vision PRIVATE ${OpenCV_INCLUDE_DIRS})
target_link_libraries(interciclo_vision PRIVATE ${OpenCV_LIBS})