# Makefile for Compiling video_processor.c to WebAssembly using Emscripten

# Compiler
CC = emcc

# Source files
SRC = video_processor.c

# Output files
OUT_JS = video_processor.js
OUT_WASM = video_processor.wasm

# Exported C functions (add or remove functions as needed)
EXPORT_FUNCTIONS = _initialize,_push_frame,_finish_processing,_get_image,_shutdownAndRelease

# Exported runtime methods (keep necessary methods only)
EXPORT_RUNTIME_METHODS = ccall,cwrap,getValue,setValue

# Compilation flags
CFLAGS = -O3 \
         -s WASM=1 \
         -s MODULARIZE=1 \
         -s 'EXPORT_NAME="createModule"' \
         -s EXPORTED_FUNCTIONS='[$(EXPORT_FUNCTIONS)]' \
         -s EXPORTED_RUNTIME_METHODS='["$(EXPORT_RUNTIME_METHODS)"]' \
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
