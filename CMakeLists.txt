cmake_minimum_required(VERSION 4.0)
project(Rimera)

if (APPLE)
    enable_language(OBJCXX)
endif ()

set(CMAKE_CXX_STANDARD 23)

function(rimera_disable_exceptions target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else ()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif ()
endfunction()

add_library(Rimera STATIC
        inc/rimera_result.hpp
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
        src/rimera_texture.cpp
        inc/rimera_builders.hpp
        inc/rimera_sampler.hpp
        inc/rimera_heap.hpp
        inc/rimera_render_graph.hpp
        src/rimera_heap.cpp
        src/rimera_render_graph.cpp
        src/rimera_device_common.cpp
)
rimera_disable_exceptions(Rimera)
target_compile_definitions(Rimera PUBLIC
        $<$<CONFIG:Debug>:RIMERA_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:RIMERA_RELEASE>)
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
            src/metal/rimera_swapchain.cpp
            src/metal/rimera_residency.cpp
            src/metal/rimera_command.cpp
            src/metal/rimera_texture.cpp
            src/metal/rimera_shader.cpp
            src/metal/rimera_buffer.cpp
            src/metal/rimera_as.cpp
            src/metal/rimera_sampler.cpp
            src/metal/rimera_heap.cpp
    )
elseif (WIN32)
    target_compile_definitions(Rimera PUBLIC RIMERA_WINDOWS)

    target_sources(Rimera
            PRIVATE
            src/d3d12/rimera_device.cpp
            src/d3d12/rimera_queue.cpp
            src/d3d12/rimera_swapchain.cpp
            src/d3d12/rimera_residency.cpp
            src/d3d12/rimera_command.cpp
            src/d3d12/rimera_texture.cpp
            src/d3d12/rimera_shader.cpp
            src/d3d12/rimera_buffer.cpp
            src/d3d12/rimera_as.cpp
            src/d3d12/rimera_d3d12.hpp
            src/d3d12/rimera_sampler.cpp
            src/d3d12/rimera_heap.cpp
            src/d3d12/rimera_d3d12.cpp)
    set_source_files_properties(src/d3d12/rimera_d3d12.cpp
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif ()

include(FetchContent)

add_subdirectory(external)
add_subdirectory(examples)
