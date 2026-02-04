/**
 * JS functions imported by WASM (C calls these)
 *
 * These are linked at compile time via --js-library.
 * The worker must implement the actual handlers and pass them
 * to the module at instantiation.
 */

mergeInto(LibraryManager.library, {

  /**
   * Post an error from C to JS
   * @param {number} error_code - ERROR_* constant from effect_stack_api.h
   * @param {number} effect_id - Which effect caused error (-1 if N/A)
   * @param {number} param_idx - Which param failed (-1 if N/A)
   * @param {number} message_ptr - Pointer to C string
   */
  js_post_error: function(error_code, effect_id, param_idx, message_ptr) {
    const message = UTF8ToString(message_ptr);
    if (Module.onError) {
      Module.onError(error_code, effect_id, param_idx, message);
    } else {
      console.error('[WASM]', { error_code, effect_id, param_idx, message });
    }
  },

  /**
   * Clear auto-placed effects from UI stack
   * @param {number} stack_type - STACK_GRADIENT or STACK_EROSION
   */
  js_clear_auto_effects: function(stack_type) {
    if (Module.onClearAutoEffects) {
      Module.onClearAutoEffects(stack_type);
    }
  },

  /**
   * Push an auto-recommended effect to UI stack
   * @param {number} stack_type - STACK_GRADIENT or STACK_EROSION
   * @param {number} effect_id - Effect ID to add
   * @param {number} params_ptr - Pointer to uint8 array
   * @param {number} param_count - Number of uint8 params
   */
  js_push_auto_effect: function(stack_type, effect_id, params_ptr, param_count) {
    if (Module.onPushAutoEffect) {
      const params = [];
      for (let i = 0; i < param_count; i++) {
        params.push(HEAPU8[params_ptr + i]);
      }
      Module.onPushAutoEffect(stack_type, effect_id, params);
    }
  },

  /**
   * Set source timing info (fade in/out durations from GIF analysis)
   * @param {number} stack_type - STACK_GRADIENT or STACK_EROSION
   * @param {number} fade_in_time - Fade in duration (seconds)
   * @param {number} fade_out_time - Fade out duration (seconds)
   * @param {number} total_duration - Total animation duration (seconds)
   */
  js_set_source_timing: function(stack_type, fade_in_time, fade_out_time, total_duration) {
    if (Module.onSetSourceTiming) {
      Module.onSetSourceTiming(stack_type, fade_in_time, fade_out_time, total_duration);
    }
  }

});
