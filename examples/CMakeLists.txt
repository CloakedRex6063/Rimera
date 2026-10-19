file(GLOB_RECURSE RIMERA_ASSET_FILES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/assets/*)

file(GLOB_RECURSE RIMERA_SHADER_FILES
        CONFIGURE_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/shaders/*)

function(copy_assets dest)
    if (APPLE)
        set(asset_destination "$<TARGET_BUNDLE_CONTENT_DIR:${dest}>/Resources/assets")
        set(shader_destination "$<TARGET_BUNDLE_CONTENT_DIR:${dest}>/Resources/shaders")
    else ()
        set(asset_destination "$<TARGET_FILE_DIR:${dest}>/assets")
        set(shader_destination "$<TARGET_FILE_DIR:${dest}>/shaders")
    endif ()
    add_custom_command(TARGET ${dest} POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/assets"
            "${asset_destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_FUNCTION_LIST_DIR}/shaders"
            "${shader_destination}"
            VERBATIM)
    set_property(TARGET ${dest} APPEND PROPERTY LINK_DEPENDS ${RIMERA_SHADER_FILES})
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
    else ()
        add_executable(${name})
    endif ()
    rimera_disable_exceptions(${name})
    copy_runtime_dlls(${name})
    target_link_libraries(${name} PUBLIC Rimera Utility)
    copy_assets(${name})
endfunction()

add_subdirectory(utility)
add_subdirectory(mesh_shader)
