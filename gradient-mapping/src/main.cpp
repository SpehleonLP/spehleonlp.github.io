/*
 * Native CLI entry point for the effect stack.
 *
 * Provides stub implementations of the JS extern functions and a command-line
 * interface to load images, build effect stacks, and write output PNGs.
 *
 * Build: cmake -B build_cli -G Ninja && ninja -C build_cli
 * Usage: ./build_cli/effect_stack_cli -i input.png -o output.png -e 0x20:140,0
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <sstream>

#include <cxxopts.hpp>

#include "effect_stack_api.h"
#include "debug_output.h"
#include "image_memo/image_memo.h"

/* Stub implementations of the JS extern functions.
 * These are declared as extern in effect_stack_api.h and normally
 * provided by Emscripten's --js-library. For native builds we
 * supply them here. */
extern "C" {

void js_post_error(int error_code, int effect_id, int param_idx,
                   const char* message) {
    fprintf(stderr, "[error] code=%d effect=0x%02X param=%d: %s\n",
            error_code, effect_id, param_idx, message);
}

void js_clear_auto_effects(int stack_type) {
    fprintf(stderr, "[info] clear_auto_effects(stack=%d)\n", stack_type);
}

void js_push_auto_effect(int stack_type, int effect_id,
                         const uint8_t* params, int param_count) {
    fprintf(stderr, "[info] push_auto_effect(stack=%d, effect=0x%02X, params=[",
            stack_type, effect_id);
    for (int i = 0; i < param_count; i++) {
        if (i > 0) fprintf(stderr, ",");
        fprintf(stderr, "%d", params[i]);
    }
    fprintf(stderr, "])\n");
}

void js_set_source_timing(int stack_type, float fade_in_time,
                          float fade_out_time, float total_duration) {
    fprintf(stderr, "[info] source_timing(stack=%d, in=%.3f, out=%.3f, dur=%.3f)\n",
            stack_type, fade_in_time, fade_out_time, total_duration);
}

/* From stb_image — just declare what we need */
unsigned char* stbi_load(const char* filename, int* x, int* y, int* channels_in_file, int desired_channels);
void stbi_image_free(void* retval_from_stbi_load);

/* From stb_image_write */
int stbi_write_png(const char* filename, int w, int h, int comp, const void* data, int stride_in_bytes);

} /* extern "C" */

/* Parse an effect spec string: "0x20:140.0" -> effect_id=0x20, params=[140,0]
 * Uses '.' as param delimiter to avoid conflicts with cxxopts comma splitting. */
static bool parse_effect_spec(const std::string& spec, int& effect_id,
                              std::vector<uint8_t>& params) {
    size_t colon = spec.find(':');
    std::string id_str = (colon != std::string::npos) ? spec.substr(0, colon) : spec;

    /* Parse effect ID (hex or decimal) */
    char* end = nullptr;
    long id = strtol(id_str.c_str(), &end, 0);
    if (end == id_str.c_str() || id < 0 || id > 0xFF) {
        fprintf(stderr, "Invalid effect ID: %s\n", id_str.c_str());
        return false;
    }
    effect_id = (int)id;

    /* Parse params if present (separated by '.') */
    params.clear();
    if (colon != std::string::npos) {
        std::string param_str = spec.substr(colon + 1);
        std::istringstream ss(param_str);
        std::string token;
        while (std::getline(ss, token, '.')) {
            long val = strtol(token.c_str(), &end, 0);
            if (val < 0 || val > 255) {
                fprintf(stderr, "Param value out of range [0,255]: %s\n", token.c_str());
                return false;
            }
            params.push_back((uint8_t)val);
        }
    }
    return true;
}

int main(int argc, char* argv[]) {
    cxxopts::Options options("effect_stack_cli",
        "Native CLI for the effect stack processing pipeline");

    options.add_options()
        ("i,input", "Source image path (required)", cxxopts::value<std::string>())
        ("o,output", "Output path prefix (e.g. dir/lic_). 'output.png' is appended for main output.",
         cxxopts::value<std::string>()->default_value("./"))
        ("s,stack", "Stack type: erosion or gradient", cxxopts::value<std::string>()->default_value("erosion"))
        ("e,effect", "Effect spec: id:p0.p1.p2 (repeatable, hex ID ok, '.' separates params)",
         cxxopts::value<std::vector<std::string>>())
        ("q,quantization", "Source quantization (0.0=1bit, 1.0=8bit)", cxxopts::value<float>()->default_value("1"))
        ("h,help", "Print usage")
    ;

    options.parse_positional({"input"});

    cxxopts::ParseResult result;
    try {
        result = options.parse(argc, argv);
    } catch (const cxxopts::exceptions::exception& e) {
        fprintf(stderr, "Error: %s\n", e.what());
        fprintf(stderr, "%s\n", options.help().c_str());
        return 1;
    }

    if (result.count("help") || !result.count("input")) {
        printf("%s\n", options.help().c_str());
        return result.count("help") ? 0 : 1;
    }

    std::string input_path = result["input"].as<std::string>();
    std::string output_prefix = result["output"].as<std::string>();
    std::string stack_str = result["stack"].as<std::string>();
    float quantization = result["quantization"].as<float>();

    /* Determine stack type */
    int stack_type;
    if (stack_str == "erosion") {
        stack_type = STACK_EROSION;
    } else if (stack_str == "gradient") {
        stack_type = STACK_GRADIENT;
    } else {
        fprintf(stderr, "Unknown stack type: %s (use 'erosion' or 'gradient')\n",
                stack_str.c_str());
        return 1;
    }

    /* Configure debug output prefix */
    set_debug_output_prefix(output_prefix.c_str());

    /* Initialize catalog */
    init_catalog();

    /* Set source path and analyze */
    set_source_path(stack_type, input_path.c_str());

    float quant_params[] = { quantization };
    analyze_source(stack_type, quant_params, 1);

    /* Build effect stack */
    stack_begin(stack_type);

    if (result.count("effect")) {
        auto& effects = result["effect"].as<std::vector<std::string>>();
        for (const auto& spec : effects) {
            int eid;
            std::vector<uint8_t> params;
            if (!parse_effect_spec(spec, eid, params)) {
                return 1;
            }
            fprintf(stderr, "Pushing effect 0x%02X with %zu params\n", eid, params.size());
            push_effect(eid, params.data(), (int)params.size());
        }
    }

    debug_print_stack(stack_type);

    int out_w = 0, out_h = 0;
    uint8_t* pixels = stack_end(&out_w, &out_h);

    if (!pixels || out_w <= 0 || out_h <= 0) {
        fprintf(stderr, "Stack processing failed (no output)\n");
        return 1;
    }

    fprintf(stderr, "Output: %dx%d\n", out_w, out_h);

    /* Write output PNG (RGBA) using prefix + "output.png" */
    std::string final_output = output_prefix + "output.png";
    if (!stbi_write_png(final_output.c_str(), out_w, out_h, 4, pixels, out_w * 4)) {
        fprintf(stderr, "Failed to write output PNG: %s\n", final_output.c_str());
        return 1;
    }

    fprintf(stderr, "Wrote %s\n", final_output.c_str());
    return 0;
}
