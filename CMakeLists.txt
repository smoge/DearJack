cmake_minimum_required(VERSION 3.10)
cmake_policy(SET CMP0072 NEW)
cmake_policy(SET CMP0075 NEW)
project(DearJack)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Set Clang as the compiler
set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Find packages
find_package(OpenGL REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(GLFW REQUIRED glfw3)
pkg_check_modules(JACK REQUIRED jack)
find_package(LLVM REQUIRED CONFIG)

# Include LLVM directories and definitions
list(APPEND CMAKE_MODULE_PATH "${LLVM_DIR}")
include_directories(${LLVM_INCLUDE_DIRS})
separate_arguments(LLVM_DEFINITIONS_LIST NATIVE_COMMAND ${LLVM_DEFINITIONS})
add_definitions(${LLVM_DEFINITIONS_LIST})

# Source files
set(IMGUI_SRC
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_draw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_demo.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_tables.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/imgui_widgets.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_glfw.cpp
    ${CMAKE_SOURCE_DIR}/external/imgui/backends/imgui_impl_opengl3.cpp
)
file(GLOB SRC_FILES ${CMAKE_SOURCE_DIR}/src/*.cpp)

# Add executable
add_executable(${PROJECT_NAME} ${SRC_FILES} ${IMGUI_SRC})

# Include directories
target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/external/imgui
    ${CMAKE_SOURCE_DIR}/external/imgui/backends
    ${GLFW_INCLUDE_DIRS}
    ${JACK_INCLUDE_DIRS}
    ${LLVM_INCLUDE_DIRS}
)

# Link libraries
target_link_libraries(${PROJECT_NAME} PRIVATE ${OPENGL_LIBRARIES} ${GLFW_LIBRARIES} ${JACK_LIBRARIES} ${LLVM_LIBRARIES})
