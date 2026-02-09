cmake_minimum_required(VERSION 3.14)
project(effect_stack CXX)

set(CMAKE_CXX_STANDARD 20)

# Use GLOB to find source files (including subdirectories)
file(GLOB SOURCES "*.cpp")
list(FILTER SOURCES EXCLUDE REGEX "main\\.cpp$")
file(GLOB HEADERS "*.h")
file(GLOB SOURCES_SUBDIR "sources/*.cpp")
file(GLOB HEADERS_SUBDIR "sources/*.h")
file(GLOB COMMANDS_SOURCES "commands/*.cpp")
file(GLOB COMMANDS_HEADERS "commands/*.h")
file(GLOB IMAGE_MEMO_SOURCES "image_memo/*.cpp")
file(GLOB IMAGE_MEMO_HEADERS "image_memo/*.h")
file(GLOB EXPLORE_SOURCES "explore/*.cpp")
file(GLOB EXPLORE_HEADERS "explore/*.h")
list(APPEND SOURCES ${SOURCES_SUBDIR} ${COMMANDS_SOURCES} ${IMAGE_MEMO_SOURCES} ${EXPLORE_SOURCES})
list(APPEND HEADERS ${HEADERS_SUBDIR} ${COMMANDS_HEADERS} ${IMAGE_MEMO_HEADERS} ${EXPLORE_HEADERS})

# Emscripten-specific settings
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")

    # Exported functions (WASM -> JS)
    set(EXPORTED_FUNCTIONS
        _init_catalog
        _set_source_path
        _set_source_changed
        _stack_begin
        _push_effect
        _stack_end
        _analyze_source
        _malloc
        _free
    )

    # Join exported functions into a comma-separated list
    list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCTIONS_STR)

    # Emscripten compilation flags
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -O3 -fstrict-aliasing")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
        -s WASM=1 \
        -s MODULARIZE=1 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MAXIMUM_MEMORY=512MB \
        -s EXPORT_NAME='createEffectStack' \
        -s EXPORTED_FUNCTIONS='[${EXPORTED_FUNCTIONS_STR}]' \
        -s EXPORTED_RUNTIME_METHODS='[ccall,cwrap,getValue,setValue,FS,UTF8ToString]' \
        -s FORCE_FILESYSTEM=1 \
        -s DEFAULT_LIBRARY_FUNCS_TO_INCLUDE='[$dynCall]' \
        --js-library ${CMAKE_SOURCE_DIR}/js_imports.js")

    # Output files
    set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../scripts")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

    add_executable(effect_stack ${SOURCES} ${HEADERS})

    target_include_directories(effect_stack PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sources
        ${CMAKE_CURRENT_SOURCE_DIR}/commands
        ${CMAKE_CURRENT_SOURCE_DIR}/image_memo
    )

    set_target_properties(effect_stack PROPERTIES
        OUTPUT_NAME "effect_stack"
        SUFFIX ".js"
    )
else()
    # Native compilation for IDE development/debugging
    add_library(effect_stack_lib STATIC ${SOURCES} ${HEADERS})

    target_include_directories(effect_stack_lib PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sources
        ${CMAKE_CURRENT_SOURCE_DIR}/commands
        ${CMAKE_CURRENT_SOURCE_DIR}/image_memo
    )

    target_compile_options(effect_stack_lib PRIVATE
        -Wall
        -Wextra
        -Wpedantic
        -fstrict-aliasing
    )

    # --- Native CLI executable ---
    include(FetchContent)
    FetchContent_Declare(
        cxxopts
        GIT_REPOSITORY https://github.com/jarro2783/cxxopts.git
        GIT_TAG        v3.1.1
    )
    FetchContent_MakeAvailable(cxxopts)

    add_executable(effect_stack_cli main.cpp)

    target_include_directories(effect_stack_cli PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}
        ${CMAKE_CURRENT_SOURCE_DIR}/sources
        ${CMAKE_CURRENT_SOURCE_DIR}/commands
        ${CMAKE_CURRENT_SOURCE_DIR}/image_memo
    )

    target_link_libraries(effect_stack_cli PRIVATE
        effect_stack_lib
        cxxopts::cxxopts
        m
    )
endif()

# For IDEs: group source files
source_group("Source Files" FILES ${SOURCES})
source_group("Header Files" FILES ${HEADERS})
source_group("Sources\\Source Files" FILES ${SOURCES_SUBDIR})
source_group("Sources\\Header Files" FILES ${HEADERS_SUBDIR})
source_group("ImageMemo\\Source Files" FILES ${IMAGE_MEMO_SOURCES})
source_group("ImageMemo\\Header Files" FILES ${IMAGE_MEMO_HEADERS})
