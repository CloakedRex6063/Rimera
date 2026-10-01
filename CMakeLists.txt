cmake_minimum_required(VERSION 4.0)
project(Rimera)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 23)

add_library(Rimera STATIC
        inc/rimera_as.hpp
        inc/rimera_buffer.hpp
        inc/rimera_command.hpp
        inc/rimera_device.hpp
        inc/rimera_enum_flags.hpp
        inc/rimera_helpers.hpp
        inc/rimera_queue.hpp
        inc/rimera_residency.hpp
        inc/rimera_shader.hpp
        inc/rimera_structs.hpp
        inc/rimera_texture.hpp
        src/rimera_helpers.cpp
        inc/rimera_builders.hpp
        inc/rimera_sampler.hpp
        inc/rimera_texture_view.hpp
)
target_include_directories(Rimera PUBLIC inc)
target_precompile_headers(Rimera PUBLIC ${CMAKE_SOURCE_DIR}/src/rimera_pch.hpp)

if (APPLE)
    target_compile_definitions(Rimera PUBLIC RIMERA_MAC)
    target_sources(Rimera
            PRIVATE
            src/metal/rimera_metal.hpp
            src/rimera_pch.hpp
            src/metal/rimera_metal.mm
            src/metal/rimera_device.cpp
            src/metal/rimera_queue.cpp
            src/metal/rimera_residency.cpp
            src/metal/rimera_command.cpp
            src/metal/rimera_texture.cpp
            src/metal/rimera_shader.cpp
            src/metal/rimera_buffer.cpp
            src/metal/rimera_as.cpp
            src/metal/rimera_sampler.cpp
            src/metal/rimera_texture_view.cpp
   )
elseif (WIN32)
    target_compile_definitions(Rimera PUBLIC RIMERA_WINDOWS)

    target_sources(Rimera
            PRIVATE
            src/d3d12/rimera_device.cpp
            src/d3d12/rimera_queue.cpp
            src/d3d12/rimera_residency.cpp
            src/d3d12/rimera_command.cpp
            src/d3d12/rimera_texture.cpp
            src/d3d12/rimera_shader.cpp
            src/d3d12/rimera_buffer.cpp
            src/d3d12/rimera_as.cpp
            src/d3d12/rimera_d3d12.hpp
            src/d3d12/rimera_sampler.cpp
            src/d3d12/rimera_d3d12.cpp
            src/d3d12/rimera_texture_view.cpp)
endif ()

include(FetchContent)

add_subdirectory(external)
enable_testing()
add_subdirectory(examples)

