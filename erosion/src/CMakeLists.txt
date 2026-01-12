cmake_minimum_required(VERSION 3.10)
project(video_processor C)

set(CMAKE_C_STANDARD 11)

# Source files
set(SOURCES
    video_processor.c
    create_envelopes.c
    create_gradient.c
    smart_blur.c
)

# Header files (for IDE navigation)
set(HEADERS
    create_envelopes.h
    create_gradient.h
    smart_blur.h
)

# Emscripten-specific settings
if(EMSCRIPTEN)
    set(CMAKE_EXECUTABLE_SUFFIX ".js")

    # Exported functions
    set(EXPORTED_FUNCTIONS
        _initialize
        _GetMetadata
        _push_frame
        _finishPushingFrames
        _computeGradient
        _get_image
        _shutdownAndRelease
        _malloc
        _free
    )

    # Join exported functions into a comma-separated list
    list(JOIN EXPORTED_FUNCTIONS "," EXPORTED_FUNCTIONS_STR)

    # Emscripten compilation flags
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -O3")
    set(CMAKE_EXE_LINKER_FLAGS "${CMAKE_EXE_LINKER_FLAGS} \
        -s WASM=1 \
        -s MODULARIZE=1 \
        -s ALLOW_MEMORY_GROWTH=1 \
        -s MAXIMUM_MEMORY=1GB \
        -s EXPORT_NAME='createModule' \
        -s EXPORTED_FUNCTIONS='[${EXPORTED_FUNCTIONS_STR}]' \
        -s EXPORTED_RUNTIME_METHODS='[ccall,cwrap,getValue,setValue]'")

    # Output files
    set(OUTPUT_DIR "${CMAKE_SOURCE_DIR}/../scripts")
    set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${OUTPUT_DIR})

    add_executable(video_processor ${SOURCES} ${HEADERS})

    # Set output name
    set_target_properties(video_processor PROPERTIES
        OUTPUT_NAME "video_processor"
        SUFFIX ".js"
    )
else()
    # Native compilation for IDE development/debugging
    # This won't produce working output but helps with code navigation and error checking
    add_library(video_processor_lib STATIC ${SOURCES} ${HEADERS})

    # Add include directories if needed
    target_include_directories(video_processor_lib PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

    # Enable warnings
    target_compile_options(video_processor_lib PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )
endif()

# For IDEs: group source files
source_group("Source Files" FILES ${SOURCES})
source_group("Header Files" FILES ${HEADERS})
