cmake_minimum_required(VERSION 3.14)
project(backend_notes)

set(CMAKE_CXX_STANDARD 17)

set(THIRD_PARTY_DIR ${PROJECT_SOURCE_DIR}/third_party)
set(SRC_DIR ${PROJECT_SOURCE_DIR}/src)
set(INCLUDE_DIR ${PROJECT_SOURCE_DIR}/include)

find_package(SDL2 REQUIRED)
include_directories(${SDL2_INCLUDE_DIRS})

find_package(OpenGL REQUIRED)
find_package(GLEW REQUIRED)

find_package(PostgreSQL REQUIRED)
include_directories(${PostgreSQL_INCLUDE_DIRS})

find_path(LIBPQXX_INCLUDE_DIR NAMES pqxx/pqxx)
find_library(LIBPQXX_LIBRARY NAMES pqxx)

if(LIBPQXX_INCLUDE_DIR AND LIBPQXX_LIBRARY)
    include_directories(${LIBPQXX_INCLUDE_DIR})
    message(STATUS "libpqxx found: ${LIBPQXX_LIBRARY}")
else()
    message(FATAL_ERROR "libpqxx not found. Install it: sudo apt install libpqxx-dev")
endif()

include_directories(${INCLUDE_DIR})

add_library(imgui STATIC
    ${THIRD_PARTY_DIR}/imgui/imgui.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_draw.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_demo.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_tables.cpp
    ${THIRD_PARTY_DIR}/imgui/imgui_widgets.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_sdl2.cpp
    ${THIRD_PARTY_DIR}/imgui/backends/imgui_impl_opengl3.cpp
)
target_include_directories(imgui PUBLIC ${THIRD_PARTY_DIR}/imgui)
target_compile_options(imgui PRIVATE -fPIC)

add_library(implot STATIC
    ${THIRD_PARTY_DIR}/implot/implot.cpp
    ${THIRD_PARTY_DIR}/implot/implot_demo.cpp
    ${THIRD_PARTY_DIR}/implot/implot_items.cpp
)
target_include_directories(implot PUBLIC ${THIRD_PARTY_DIR}/implot)
target_link_libraries(implot PRIVATE imgui)
target_compile_options(implot PRIVATE -fPIC)

add_library(database STATIC ${SRC_DIR}/database.cpp)
target_include_directories(database PUBLIC ${INCLUDE_DIR})
target_link_libraries(database PRIVATE ${PostgreSQL_LIBRARIES} ${LIBPQXX_LIBRARY})

add_executable(main
    ${SRC_DIR}/main.cpp
    ${SRC_DIR}/database.cpp
)
target_link_libraries(main PRIVATE
    zmq
    imgui
    implot
    ${SDL2_LIBRARIES}
    ${OPENGL_LIBRARIES}
    ${GLEW_LIBRARIES}
    ${PostgreSQL_LIBRARIES}
    ${LIBPQXX_LIBRARY}
)
