add_library(DXC::Compiler SHARED IMPORTED GLOBAL)
set_target_properties(DXC::Compiler PROPERTIES
        INTERFACE_INCLUDE_DIRECTORIES
        "${PROJECT_SOURCE_DIR}/external/dxil/include"
)
if (WIN32)
    if (CMAKE_SYSTEM_PROCESSOR MATCHES "ARM64|arm64|aarch64")
        set(DXC_WINDOWS_ARCH arm64)
    else ()
        set(DXC_WINDOWS_ARCH x64)
    endif ()
    FetchContent_Declare(
            DXC_Windows
            URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/dxc_2026_09_29.zip
            URL_HASH SHA256=ad31b1fc8443175d204f77a611fdb3ef2ec42759bdc2f1167368de24a4a7e7f1
    )
    FetchContent_MakeAvailable(DXC_Windows)
    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_IMPLIB
            "${dxc_windows_SOURCE_DIR}/lib/${DXC_WINDOWS_ARCH}/dxcompiler.lib"
            IMPORTED_LOCATION
            "${dxc_windows_SOURCE_DIR}/bin/${DXC_WINDOWS_ARCH}/dxcompiler.dll"
    )
    set(DXC_EXTRA_RUNTIME_FILES "${dxc_windows_SOURCE_DIR}/bin/${DXC_WINDOWS_ARCH}/dxil.dll" CACHE INTERNAL "")

    add_library(WindowsLean INTERFACE)
    target_include_directories(WindowsLean INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/win32)
    target_link_libraries(Rimera PUBLIC WindowsLean)
elseif (APPLE)
    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_LOCATION
            "${PROJECT_SOURCE_DIR}/external/dxil/libdxcompiler.dylib"
    )
else ()
    FetchContent_Declare(
            DXC_Linux
            URL https://github.com/microsoft/DirectXShaderCompiler/releases/download/v1.9.2609/linux_dxc_2026_09_28.x86_x64.tar.gz
            URL_HASH SHA256=96faadc7f5c282d2ffda49804beb4c3ee38127bc252b723234e3c5cdf7aa39a1
    )
    FetchContent_MakeAvailable(DXC_Linux)
    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_LOCATION
            "${dxc_linux_SOURCE_DIR}/lib/libdxcompiler.so"
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
            AgilitySDK
            URL https://api.nuget.org/v3-flatcontainer/microsoft.direct3d.d3d12/1.619.6/microsoft.direct3d.d3d12.1.619.6.nupkg
            DOWNLOAD_NAME microsoft.direct3d.d3d12.1.619.6.zip
            URL_HASH SHA256=08f0489281401aa430fc37322d6c3fc98a8025175aacd714c10d562f4963f1e9
    )
    FetchContent_MakeAvailable(AgilitySDK)
    set(D3D12_RUNTIME_DIR "${agilitysdk_SOURCE_DIR}/build/native/bin/${DXC_WINDOWS_ARCH}" CACHE INTERNAL "")

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
elseif (RIMERA_BACKEND STREQUAL "Vulkan")
    FetchContent_Declare(
            Vulkan-Headers
            GIT_REPOSITORY https://github.com/KhronosGroup/Vulkan-Headers
            GIT_TAG v1.4.363
    )
    FetchContent_MakeAvailable(Vulkan-Headers)

    set(VOLK_PULL_IN_VULKAN OFF CACHE BOOL "" FORCE)
    FetchContent_Declare(
            volk
            GIT_REPOSITORY https://github.com/zeux/volk
            GIT_TAG vulkan-sdk-1.4.363.0
    )
    FetchContent_MakeAvailable(volk)
    if (WIN32)
        target_compile_definitions(volk PUBLIC VK_USE_PLATFORM_WIN32_KHR)
    endif ()
    target_link_libraries(volk PUBLIC Vulkan::Headers)

    FetchContent_Declare(
            VulkanMemoryAllocator
            GIT_REPOSITORY https://github.com/GPUOpen-LibrariesAndSDKs/VulkanMemoryAllocator
            GIT_TAG v3.4.0
    )
    FetchContent_MakeAvailable(VulkanMemoryAllocator)

    target_link_libraries(Rimera PUBLIC Vulkan::Headers volk GPUOpen::VulkanMemoryAllocator)
    if (WIN32)
        target_link_libraries(Rimera PRIVATE dxgi)
    endif ()
endif ()

function(copy_runtime_dlls target)
    if (RIMERA_BACKEND STREQUAL "D3D12")
        add_custom_target(copy_runtime_${target}
                COMMAND ${CMAKE_COMMAND} -E make_directory
                "$<TARGET_FILE_DIR:${target}>/D3D12"
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${D3D12_RUNTIME_DIR}/D3D12Core.dll"
                "${D3D12_RUNTIME_DIR}/d3d12SDKLayers.dll"
                "${D3D12_RUNTIME_DIR}/D3D12StateObjectCompiler.dll"
                "$<TARGET_FILE_DIR:${target}>/D3D12"
                VERBATIM
        )
        add_custom_target(copy_runtime_dxcompiler_${target}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:DXC::Compiler>"
                ${DXC_EXTRA_RUNTIME_FILES}
                "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
        )
        add_dependencies(${target} copy_runtime_${target})
        add_dependencies(${target} copy_runtime_dxcompiler_${target})
    elseif (RIMERA_BACKEND STREQUAL "Vulkan")
        add_custom_target(copy_runtime_dxcompiler_${target}
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:DXC::Compiler>"
                ${DXC_EXTRA_RUNTIME_FILES}
                "$<TARGET_FILE_DIR:${target}>"
                VERBATIM
        )
        add_dependencies(${target} copy_runtime_dxcompiler_${target})
        if (UNIX AND NOT APPLE)
            set_target_properties(${target} PROPERTIES
                    BUILD_RPATH "$ORIGIN"
                    INSTALL_RPATH "$ORIGIN")
        endif ()
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
