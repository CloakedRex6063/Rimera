FetchContent_Declare(
        GLM
        GIT_REPOSITORY https://github.com/g-truc/glm
        GIT_TAG 1.0.3
)
FetchContent_MakeAvailable(GLM)
target_link_libraries(Utility PUBLIC glm::glm)

FetchContent_Declare(
        FastGLTF
        GIT_REPOSITORY https://github.com/spnda/fastgltf
        GIT_TAG v0.9.0
)
FetchContent_MakeAvailable(FastGLTF)
target_link_libraries(Utility PUBLIC fastgltf::fastgltf)

FetchContent_Declare(
        MeshOpt
        GIT_REPOSITORY https://github.com/zeux/meshoptimizer
        GIT_TAG v1.2
)
FetchContent_MakeAvailable(MeshOpt)
target_link_libraries(Utility PUBLIC meshoptimizer)

FetchContent_Declare(
        STB
        GIT_REPOSITORY https://github.com/nothings/stb
        GIT_TAG master
)
FetchContent_MakeAvailable(STB)
target_include_directories(Utility PUBLIC ${stb_SOURCE_DIR})

FetchContent_Declare(
        mikktspace
        GIT_REPOSITORY https://github.com/mmikk/MikkTSpace
        GIT_TAG master
)
FetchContent_MakeAvailable(mikktspace)
add_library(mikktspace STATIC ${mikktspace_SOURCE_DIR}/mikktspace.c)
target_include_directories(mikktspace PUBLIC ${mikktspace_SOURCE_DIR})
target_link_libraries(Utility PUBLIC mikktspace)

FetchContent_Declare(
        ImGui
        GIT_REPOSITORY https://github.com/ocornut/imgui
        GIT_TAG v1.91.9b
)
FetchContent_MakeAvailable(ImGui)
add_library(imgui STATIC
        ${imgui_SOURCE_DIR}/imgui.cpp
        ${imgui_SOURCE_DIR}/imgui_demo.cpp
        ${imgui_SOURCE_DIR}/imgui_draw.cpp
        ${imgui_SOURCE_DIR}/imgui_tables.cpp
        ${imgui_SOURCE_DIR}/imgui_widgets.cpp)
target_include_directories(imgui PUBLIC ${imgui_SOURCE_DIR})
target_link_libraries(Utility PUBLIC imgui)

FetchContent_Declare(
        ImPlot
        GIT_REPOSITORY https://github.com/epezent/implot
        GIT_TAG v1.0
)
FetchContent_MakeAvailable(ImPlot)
add_library(implot STATIC
        ${implot_SOURCE_DIR}/implot.cpp
        ${implot_SOURCE_DIR}/implot_demo.cpp
        ${implot_SOURCE_DIR}/implot_items.cpp)
target_include_directories(implot PUBLIC ${implot_SOURCE_DIR})
target_link_libraries(implot PUBLIC imgui)
target_link_libraries(Utility PUBLIC implot)
