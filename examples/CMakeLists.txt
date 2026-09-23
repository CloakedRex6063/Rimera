file(GLOB_RECURSE RIMERA_ASSET_FILES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/*)

file(GLOB_RECURSE RIMERA_SHADER_FILES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*)

function(copy_assets dest)
    if (APPLE)
        set(asset_destination "$<TARGET_BUNDLE_CONTENT_DIR:${dest}>/Resources/assets")
    else ()
        set(asset_destination "$<TARGET_FILE_DIR:${dest}>/assets")
    endif ()
    add_custom_command(TARGET ${dest} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets"
            "${asset_destination}"
            VERBATIM)
endfunction()

function(create_example name)
    if (APPLE)
        add_executable(${name} MACOSX_BUNDLE)
        set_target_properties(${name} PROPERTIES
                MACOSX_BUNDLE_BUNDLE_NAME "${name}"
                MACOSX_BUNDLE_GUI_IDENTIFIER "com.rimera.${name}"
                MACOSX_BUNDLE_BUNDLE_VERSION "1"
                MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        )
        get_filename_component(METAL_IRCONVERTER_LIBRARY_DIR "${METAL_IRCONVERTER_LIBRARY}" DIRECTORY)
        set_target_properties(${name} PROPERTIES
                BUILD_WITH_INSTALL_RPATH TRUE
                INSTALL_RPATH "@executable_path;${METAL_IRCONVERTER_LIBRARY_DIR}")
        add_custom_command(TARGET ${name} POST_BUILD
                COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "$<TARGET_FILE:DXC::Compiler>"
                "$<TARGET_FILE_DIR:${name}>"
                VERBATIM)
    else ()
        add_executable(${name})
    endif ()
    target_link_libraries(${name} PUBLIC Rimera Utility)
    copy_assets(${name})
endfunction()

add_subdirectory(utility)
add_subdirectory(path_tracer)
