BUILD_WASM = build
BUILD_CLI  = build_cli

all: wasm cli

wasm:
	@mkdir -p $(BUILD_WASM)
	@cd $(BUILD_WASM) && emcmake cmake .. -G "Unix Makefiles" && emmake make

cli:
	@mkdir -p $(BUILD_CLI)
	@cd $(BUILD_CLI) && cmake .. && make -j$$(nproc)

clean:
	rm -rf $(BUILD_WASM) $(BUILD_CLI)
	rm -f ../scripts/effect_stack.js ../scripts/effect_stack.wasm

.PHONY: all wasm cli clean
