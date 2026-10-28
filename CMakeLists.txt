cmake_minimum_required(VERSION 3.24)
project(XHL_Remote_Chest_PinYin LANGUAGES C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)
    set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)

set(GEN_DIR "${CMAKE_BINARY_DIR}/generated")
set(GEN_HEADERS "${GEN_DIR}/item_table.h" "${GEN_DIR}/pinyin_table.h")
set(DATA_FILES
    "${CMAKE_SOURCE_DIR}/data/items_full.json"
    "${CMAKE_SOURCE_DIR}/data/items_desc.json"
    "${CMAKE_SOURCE_DIR}/data/item_attributes.json")

add_custom_command(
    OUTPUT ${GEN_HEADERS}
    COMMAND ${CMAKE_COMMAND} -E make_directory "${GEN_DIR}"
    COMMAND ${Python3_EXECUTABLE} "${CMAKE_SOURCE_DIR}/tools/gen_tables.py"
            --data "${CMAKE_SOURCE_DIR}/data" --out "${GEN_DIR}"
    DEPENDS "${CMAKE_SOURCE_DIR}/tools/gen_tables.py" ${DATA_FILES}
    COMMENT "Generating item and pinyin tables"
    VERBATIM)

add_custom_target(tables DEPENDS ${GEN_HEADERS})

add_library(xhl_core STATIC
    src/data.c
    src/pinyin.c
    src/search.c
    src/log.c)
target_include_directories(xhl_core PUBLIC "${CMAKE_SOURCE_DIR}/src" "${GEN_DIR}")
target_link_libraries(xhl_core PUBLIC user32)
add_dependencies(xhl_core tables)

add_library(XHL-Remote-Chest-PinYin SHARED
    src/main.c
    src/plugin_abi.c
    src/game_patch.c
    src/input_hook.c
    src/ime.c
    src/game_ui.c
    src/module.c)
target_link_libraries(XHL-Remote-Chest-PinYin PRIVATE xhl_core imm32 user32 dxguid)
set_target_properties(XHL-Remote-Chest-PinYin PROPERTIES
    OUTPUT_NAME "XHL-Remote-Chest-PinYin"
    PREFIX "")

set(MOD_OUT "${CMAKE_SOURCE_DIR}/out/XHL-Remote-Chest-PinYin")

add_custom_command(TARGET XHL-Remote-Chest-PinYin POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MOD_OUT}"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "$<TARGET_FILE:XHL-Remote-Chest-PinYin>"
            "${MOD_OUT}/XHL-Remote-Chest-PinYin.dll"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/packaging/mod.json"
            "${MOD_OUT}/mod.json"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/packaging/native-plugin.ini"
            "${MOD_OUT}/native-plugin.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/packaging/pin_yin.ini"
            "${MOD_OUT}/pin_yin.ini"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/packaging/README.txt"
            "${MOD_OUT}/README.txt"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_SOURCE_DIR}/LICENSE"
            "${MOD_OUT}/LICENSE.txt"
    COMMENT "Packaging mod folder into out/XHL-Remote-Chest-PinYin")
