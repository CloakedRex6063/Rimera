cmake_minimum_required(VERSION 4.0)
project(Rimera)

set(CMAKE_CXX_STANDARD 23)

if (WIN32)
    set(RIMERA_DEFAULT_BACKEND D3D12)
elseif (APPLE)
    set(RIMERA_DEFAULT_BACKEND Metal)
else ()
    set(RIMERA_DEFAULT_BACKEND Vulkan)
endif ()
set(RIMERA_BACKEND ${RIMERA_DEFAULT_BACKEND} CACHE STRING "D3D12|Metal|Vulkan")
set_property(CACHE RIMERA_BACKEND PROPERTY STRINGS D3D12 Metal Vulkan)

if (RIMERA_BACKEND STREQUAL "D3D12")
    if (NOT WIN32)
        message(FATAL_ERROR "The D3D12 backend requires Windows")
    endif ()
elseif (RIMERA_BACKEND STREQUAL "Metal")
    if (NOT APPLE)
        message(FATAL_ERROR "The Metal backend requires macOS")
    endif ()
    enable_language(OBJCXX)
elseif (RIMERA_BACKEND STREQUAL "Vulkan")
    if (APPLE)
        message(FATAL_ERROR "The Vulkan backend is not supported on macOS")
    endif ()
else ()
    message(FATAL_ERROR "Unknown RIMERA_BACKEND '${RIMERA_BACKEND}', expected D3D12, Metal or Vulkan")
endif ()

function(rimera_disable_exceptions target)
    if (MSVC)
        target_compile_options(${target} PRIVATE /EHs-c-)
        target_compile_definitions(${target} PRIVATE _HAS_EXCEPTIONS=0)
    else ()
        target_compile_options(${target} PRIVATE -fno-exceptions)
    endif ()
endfunction()

set(RIMERA_SHADER_DIR ${CMAKE_CURRENT_SOURCE_DIR}/shaders)

function(rimera_copy_shaders target destination)
    file(GLOB rimera_shader_files CONFIGURE_DEPENDS ${RIMERA_SHADER_DIR}/*.hlsl)
    add_custom_command(TARGET ${target} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
            ${rimera_shader_files}
            "${destination}"
            VERBATIM)
    set_property(TARGET ${target} APPEND PROPERTY LINK_DEPENDS ${rimera_shader_files})
endfunction()

add_library(Rimera STATIC
        inc/rimera_result.hpp
        inc/rimera_as.hpp
        inc/rimera_buffer.hpp
        inc/rimera_command.hpp
        inc/rimera_compiler.hpp
        inc/rimera_device.hpp
        inc/rimera_enum_flags.hpp
        inc/rimera_helpers.hpp
        inc/rimera_queue.hpp
        inc/rimera_residency.hpp
        inc/rimera_shader.hpp
        inc/rimera_structs.hpp
        inc/rimera_texture.hpp
        src/rimera_compiler.cpp
        src/rimera_helpers.cpp
        src/rimera_texture_common.cpp
        inc/rimera_builders.hpp
        inc/rimera_sampler.hpp
        inc/rimera_heap.hpp
        inc/rimera_render_graph.hpp
        src/rimera_heap_common.cpp
        src/rimera_render_graph.cpp
        src/rimera_device_common.cpp
        src/rimera_queue_common.cpp
        src/rimera_residency_common.cpp
        src/rimera_buffer_common.cpp
)
rimera_disable_exceptions(Rimera)
target_compile_definitions(Rimera PUBLIC
        $<$<CONFIG:Debug>:RIMERA_DEBUG>
        $<$<NOT:$<CONFIG:Debug>>:RIMERA_RELEASE>)
target_include_directories(Rimera PUBLIC inc)
target_precompile_headers(Rimera PUBLIC ${CMAKE_SOURCE_DIR}/src/rimera_pch.hpp)

if (WIN32)
    target_compile_definitions(Rimera PUBLIC RIMERA_WINDOWS)
elseif (APPLE)
    target_compile_definitions(Rimera PUBLIC RIMERA_MAC)
else ()
    target_compile_definitions(Rimera PUBLIC RIMERA_LINUX)
endif ()

if (RIMERA_BACKEND STREQUAL "Metal")
    target_compile_definitions(Rimera PUBLIC RIMERA_METAL)
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
elseif (RIMERA_BACKEND STREQUAL "D3D12")
    target_compile_definitions(Rimera PUBLIC RIMERA_D3D12)
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
elseif (RIMERA_BACKEND STREQUAL "Vulkan")
    target_compile_definitions(Rimera PUBLIC RIMERA_VULKAN)
    target_sources(Rimera
            PRIVATE
            src/vulkan/rimera_vulkan.hpp
            src/vulkan/rimera_vulkan.cpp
            src/vulkan/rimera_vma.cpp
            src/vulkan/rimera_shader.cpp
            src/vulkan/rimera_sampler.cpp
            src/vulkan/rimera_texture.cpp
            src/vulkan/rimera_buffer.cpp
            src/vulkan/rimera_heap.cpp
            src/vulkan/rimera_swapchain.cpp
            src/vulkan/rimera_device.cpp
            src/vulkan/rimera_queue.cpp
            src/vulkan/rimera_residency.cpp
            src/vulkan/rimera_command.cpp)
    set_source_files_properties(src/vulkan/rimera_vma.cpp
            PROPERTIES SKIP_PRECOMPILE_HEADERS ON)
endif ()

include(FetchContent)

add_subdirectory(external)
add_subdirectory(examples)
