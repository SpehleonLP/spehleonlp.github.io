BUILD_DIR = build

all:
	@mkdir -p $(BUILD_DIR)
	@cd $(BUILD_DIR) && emcmake cmake .. -G "Unix Makefiles" && emmake make

clean:
	rm -rf $(BUILD_DIR)
	rm -f ../scripts/effect_stack.js ../scripts/effect_stack.wasm

.PHONY: all clean
