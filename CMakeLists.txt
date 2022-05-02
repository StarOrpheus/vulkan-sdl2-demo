cmake_minimum_required(VERSION 3.22)
project(vulkan_sdl2_demo)

set(CMAKE_CXX_STANDARD 20)

find_package(imgui CONFIG REQUIRED)
find_package(SDL2 CONFIG REQUIRED)
find_package(sdl2-mixer CONFIG REQUIRED)
find_package(sdl2-image CONFIG REQUIRED)
find_package(sdl2-ttf CONFIG REQUIRED)
find_package(fmt CONFIG REQUIRED)
find_package(SDL2PP REQUIRED)

find_path(VULKAN_HPP_INCLUDE_DIRS "vulkan/vulkan.hpp")

add_executable(vulkan_sdl2_demo main.cpp)
target_include_directories(vulkan_sdl2_demo PRIVATE ${VULKAN_HPP_INCLUDE_DIRS} ${SDL2PP_INCLUDE_DIRS})
target_link_libraries(vulkan_sdl2_demo PRIVATE imgui::imgui SDL2::SDL2 ${SDL2PP_LIBRARIES} fmt::fmt)
