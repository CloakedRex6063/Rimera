add_library(Utility ${UTILITY_SOURCES})
rimera_disable_exceptions(Utility)
target_include_directories(Utility PUBLIC inc)
target_link_libraries(Utility PUBLIC Rimera)

target_sources(Utility PRIVATE
        inc/camera.hpp
        inc/fileio.hpp
        inc/imgui_layer.hpp
        inc/mesh.hpp
        inc/model.hpp
        inc/texture.hpp
        inc/window.hpp
        src/camera.cpp
        src/fileio.cpp
        src/imgui_layer.cpp
        src/mesh.cpp
        src/model.cpp
        src/texture.cpp
        src/window.cpp)

add_subdirectory(external)

if(APPLE)
    target_link_libraries(Utility PRIVATE "-framework CoreServices")
endif()
