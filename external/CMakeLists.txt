if (APPLE)
    FetchContent_Declare(
            Metal
            GIT_REPOSITORY https://github.com/apple/metal-cpp
            GIT_TAG release/metal-cpp_macOS27_iOS27
    )
    FetchContent_MakeAvailable(Metal)

    add_library(DXC::Compiler SHARED IMPORTED GLOBAL)

    set_target_properties(DXC::Compiler PROPERTIES
            IMPORTED_LOCATION
            "${PROJECT_SOURCE_DIR}/external/dxil/libdxcompiler.dylib"
            INTERFACE_INCLUDE_DIRECTORIES
            "${PROJECT_SOURCE_DIR}/external/dxil/include"
    )

    target_link_libraries(Rimera PRIVATE DXC::Compiler)

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
endif ()
