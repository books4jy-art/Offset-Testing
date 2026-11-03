# Offset Lab: a Canvas / Sky Modloader mod (Android arm64-v8a).
cmake_minimum_required(VERSION 3.22)
project(OffsetLab CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(NOT ANDROID_ABI STREQUAL "arm64-v8a")
  message(FATAL_ERROR "Build for arm64-v8a with the Android NDK toolchain")
endif()

set(IMGUI_DIR ${CMAKE_SOURCE_DIR}/third_party/imgui)

# Link-only stand-in for Canvas's libciphered.so, which provides Dear ImGui at runtime. Built from the same Dear
# ImGui commit Canvas uses (d3c3514a, MIT); the mod records a dependency on libciphered.so but ships none of it.
add_library(ciphered SHARED
  ${IMGUI_DIR}/imgui.cpp ${IMGUI_DIR}/imgui_draw.cpp ${IMGUI_DIR}/imgui_widgets.cpp ${IMGUI_DIR}/imgui_tables.cpp
  src/linkonly/canvas_stubs.cpp)  # Canvas's icon and asset functions (declared in src/canvas_api.h)
target_include_directories(ciphered PUBLIC ${IMGUI_DIR} src)
set_target_properties(ciphered PROPERTIES OUTPUT_NAME ciphered LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/linkonly)
# Export only Dear ImGui from the stand-in (like Canvas's library), so the mod carries its own C++ runtime
target_link_options(ciphered PRIVATE -Wl,--exclude-libs,ALL)

add_library(OffsetLab SHARED src/main.cpp src/game.cpp src/symbols.cpp src/ui.cpp src/features.cpp)
set_target_properties(OffsetLab PROPERTIES OUTPUT_NAME OffsetLab)
target_include_directories(OffsetLab PRIVATE src ${IMGUI_DIR} third_party/dobby)
target_compile_options(OffsetLab PRIVATE -O2 -fvisibility=hidden -Wall -Wno-unused-function)
# System libraries first, so names they define win over anything with the same name in Canvas's libciphered.so
target_link_libraries(OffsetLab PRIVATE c m dl log ciphered
  -Wl,--whole-archive ${CMAKE_SOURCE_DIR}/third_party/dobby/arm64-v8a/libdobby.a -Wl,--no-whole-archive)
target_link_options(OffsetLab PRIVATE -Wl,--exclude-libs,ALL -Wl,--gc-sections -s)

# Canvas reads the mod's settings from a .config section and its picture from .icon
find_program(OBJCOPY NAMES llvm-objcopy PATHS ${ANDROID_TOOLCHAIN_ROOT}/bin NO_DEFAULT_PATH)
set_property(TARGET OffsetLab APPEND PROPERTY LINK_DEPENDS ${CMAKE_SOURCE_DIR}/config/config.json ${CMAKE_SOURCE_DIR}/config/icon.png)
add_custom_command(TARGET OffsetLab POST_BUILD
  COMMAND ${OBJCOPY} --add-section .config=${CMAKE_SOURCE_DIR}/config/config.json
          --set-section-flags .config=readonly,contents,data
          --add-section .icon=${CMAKE_SOURCE_DIR}/config/icon.png
          --set-section-flags .icon=readonly,contents,data $<TARGET_FILE:OffsetLab>
  COMMENT "Embedding config.json and icon.png")
