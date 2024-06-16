cmake_minimum_required(VERSION 3.10)
project(DearJack)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Find packages
find_package(OpenGL REQUIRED)
find_package(glfw3 3.3 REQUIRED)

# Include directories
include_directories(${CMAKE_SOURCE_DIR}/external/imgui)
include_directories(${CMAKE_SOURCE_DIR}/external/imgui/backends)
include_directories(${CMAKE_SOURCE_DIR}/external/jack2/common)
include_directories(${CMAKE_SOURCE_DIR}/external/jack2/jack)

# Source files
file(GLOB IMGUI_SRC
    ${CMAKE_SOURCE_DIR}/external/imgui/*.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_opengl3.cpp
)
file(GLOB SRC_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_FILES} ${IMGUI_SRC})

# Link libraries
target_link_libraries(${PROJECT_NAME} OpenGL::GL glfw jack)
