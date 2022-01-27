set (TARGET_NAME q1compile)

file (GLOB SOURCE_FILES *.cpp
  ../include/imgui/imgui_demo.cpp
  ../include/imgui/imgui_draw.cpp
  ../include/imgui/imgui.cpp
  ../include/imgui/examples/imgui_impl_dx9.cpp
  ../include/imgui/examples/imgui_impl_win32.cpp
  ../include/imgui/misc/cpp/imgui_stdlib.cpp)
file (GLOB HEADER_FILES *.h)

add_executable (${TARGET_NAME} ${SOURCE_FILES} ${HEADER_FILES})

include_directories(.)
include_directories(../include/imgui)
include_directories(../include/imgui/examples)
include_directories(../include/imgui-filebrowser)

if (WIN32)
    target_link_libraries(${TARGET_NAME} d3d9 xinput9_1_0)
    target_link_options(${TARGET_NAME} PRIVATE /SUBSYSTEM:WINDOWS)
endif (WIN32)

set_target_properties(${TARGET_NAME} PROPERTIES
  CXX_STANDARD 17
  CXX_EXTENSIONS OFF)
