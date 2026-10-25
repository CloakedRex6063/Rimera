add_library(DXC::Compiler SHARED IMPORTED GLOBAL)
set_target_properties(DXC::Compiler PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${PROJECT_SOURCE_DIR}/external/dxil/include"
)
if (WIN32)
    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_IMPLIB
            "${PROJECT_SOURCE_DIR}/external/dxil/dxcompiler.lib"
            IMPORTED_LOCATION
            "${PROJECT_SOURCE_DIR}/external/dxil/dxcompiler.dll"
    )

    add_library(WindowsLean INTERFACE)
    target_include_directories(WindowsLean INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/win32)
    target_link_libraries(Rimera PUBLIC WindowsLean)
elseif (APPLE)
    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_LOCATION
            "${PROJECT_SOURCE_DIR}/external/dxil/libdxcompiler.dylib"
    )
endif ()

if (RIMERA_BACKEND STREQUAL "Metal")
    FetchContent_Declare(
            Metal
            GIT_REPOSITORY https://github.com/apple/metal-cpp
            GIT_TAG release/metal-cpp_macOS27_iOS27
    )
    FetchContent_MakeAvailable(Metal)

    add_library(MetalProxy STATIC metal_def.cpp)
    target_include_directories(MetalProxy PUBLIC ${metal_SOURCE_DIR} ${metalext_SOURCE_DIR})
    find_library(METAL_FRAMEWORK Metal REQUIRED)
    find_library(METALKIT_FRAMEWORK MetalKit REQUIRED)
    find_library(QUARTZCORE_FRAMEWORK QuartzCore REQUIRED)
    target_link_libraries(MetalProxy PUBLIC
            "-framework Foundation"
            "-framework AppKit"
            "-framework Metal"
            "-framework QuartzCore"
    )
    target_link_libraries(Rimera PUBLIC MetalProxy)

    find_path(METAL_IRCONVERTER_INCLUDE_DIR
            NAMES metal_irconverter/metal_irconverter.h
            HINTS /usr/local/include
            REQUIRED)
    find_library(METAL_IRCONVERTER_LIBRARY
            NAMES metalirconverter
            HINTS /usr/local/lib
            REQUIRED)
    target_include_directories(Rimera PRIVATE ${METAL_IRCONVERTER_INCLUDE_DIR})
    target_link_libraries(Rimera PRIVATE ${METAL_IRCONVERTER_LIBRARY})
elseif (RIMERA_BACKEND STREQUAL "D3D12")
    FetchContent_Declare(
            DX-Headers
            GIT_REPOSITORY https://github.com/microsoft/DirectX-Headers
            GIT_TAG v1.619.5
    )
    FetchContent_MakeAvailable(DX-Headers)
    target_link_libraries(Rimera PUBLIC Microsoft::DirectX-Headers)

    FetchContent_Declare(
            D3D12MA
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/D3D12MemoryAllocator
            GIT_TAG v3.1.0
    )
    FetchContent_MakeAvailable(D3D12MA)
    target_link_libraries(Rimera PUBLIC D3D12MemoryAllocator)

    target_link_libraries(Rimera PRIVATE d3d12 dxgi)
endif ()

function(copy_runtime_dlls target)
    if (RIMERA_BACKEND STREQUAL "D3D12")
        set(D3D12_RUNTIME_DIR "${PROJECT_SOURCE_DIR}/external/d3d12")
        add_custom_target(copy_runtime_${target}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/D3D12"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:DXC::Compiler>"
                "${D3D12_RUNTIME_DIR}/D3D12Core.dll"
                "${D3D12_RUNTIME_DIR}/d3d12SDKLayers.dll"
                "${D3D12_RUNTIME_DIR}/D3D12StateObjectCompiler.dll"
                "$<TARGET_FILE_DIR:${target}>/D3D12"
                VERBATIM
        )
        add_dependencies(${target} copy_runtime_${target})
    elseif (RIMERA_BACKEND STREQUAL "Metal")
        get_filename_component(METAL_IRCONVERTER_LIBRARY_DIR "${METAL_IRCONVERTER_LIBRARY}" DIRECTORY)
        set_target_properties(${target} PROPERTIES
                BUILD_WITH_INSTALL_RPATH TRUE
                INSTALL_RPATH "@executable_path/../Frameworks;${METAL_IRCONVERTER_LIBRARY_DIR}"
        )
        add_custom_target(copy_dxc_${target}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:DXC::Compiler>"
                "$<TARGET_BUNDLE_DIR:${target}>/Contents/Frameworks"
                VERBATIM
        )
        add_dependencies(${target} copy_dxc_${target})
    endif ()
endfunction()

target_link_libraries(Rimera PRIVATE DXC::Compiler)
