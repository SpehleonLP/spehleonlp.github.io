# Makefile for Compiling video_processor.c to WebAssembly using Emscripten

# Compiler
CC = emcc

# Source files
SRC = video_processor.c create_envelopes.c create_gradient.c smart_blur.c \
      fluid_solver.c fft_blur.c normal_map.c helmholtz.c swirl.c debug_png.c flood_fill.c \
      contour_flow.c chamfer.c label_regions.c contour_extract.c contour_smooth.c gif_decoder.c \
      sdf_layered.c interp_quantized.c

# Output files
OUT_JS = ../scripts/video_processor.js
OUT_WASM = ../scripts/video_processor.wasm

# Exported C functions (add or remove functions as needed)
EXPORT_FUNCTIONS = _initialize,_GetMetadata,_push_frame,_push_gif,_finishPushingFrames,_computeGradient,_get_image,_shutdownAndRelease,_malloc,_free

# Exported runtime methods (keep necessary methods only)
EXPORT_RUNTIME_METHODS = ccall,cwrap,getValue,setValue,FS

# Compilation flags
CFLAGS = -O3 \
         -fstrict-aliasing -Wstrict-aliasing=2 -Werror=strict-aliasing \
         -Werror=incompatible-pointer-types \
         -s WASM=1 \
         -s MODULARIZE=1 \
         -s ALLOW_MEMORY_GROWTH=1 \
         -s MAXIMUM_MEMORY=1GB \
         -s FORCE_FILESYSTEM=1 \
         -s 'EXPORT_NAME="createModule"' \
         -s EXPORTED_FUNCTIONS='[$(EXPORT_FUNCTIONS)]' \
         -s EXPORTED_RUNTIME_METHODS='[$(EXPORT_RUNTIME_METHODS)]' \
         -o $(OUT_JS)

# Default target
all: $(OUT_JS) $(OUT_WASM)

# Rule to build the WebAssembly module
$(OUT_JS): $(SRC)
	$(CC) $(SRC) $(CFLAGS)

# Optional: Rule to build only the JavaScript file
js: $(OUT_JS)

# Optional: Rule to build only the WebAssembly binary
wasm: $(OUT_WASM)

# Clean up generated files
clean:
	rm -f $(OUT_JS) $(OUT_WASM)

# Declare phony targets
.PHONY: all clean js wasm
