add_library(Utility ${UTILITY_SOURCES})
target_include_directories(Utility PUBLIC inc)
target_link_libraries(Utility PUBLIC Rimera)

target_sources(Utility PRIVATE
        inc/mesh.hpp
        inc/model.hpp
        inc/texture.hpp
        inc/window.hpp
        src/mesh.cpp
        src/model.cpp
        src/texture.cpp)

add_subdirectory(external)

if(APPLE)
    set_target_properties(Utility PROPERTIES OBJCXX_STANDARD 23)
    target_sources(Utility PRIVATE src/macos/window.mm)
endif()
