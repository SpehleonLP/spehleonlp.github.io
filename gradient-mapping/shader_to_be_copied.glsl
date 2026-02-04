/*
 * PER-PIXEL TIMING SHADER
 * =======================
 *
 * This shader animates dissolve effects using two textures instead of sprite sheets.
 * Each pixel fades in and out on its own schedule, like keys on a piano being pressed
 * and released at different times to play a chord.
 *
 * THE TWO TEXTURES:
 *
 *   Erosion Texture - A timing map. Each pixel stores WHEN it appears/disappears.
 *   Gradient Ramp   - A 2D color palette. Maps position and lifetime to color.
 *
 * This approach uses ~30x less memory than sprite sheets and produces smoother
 * animation because there are no discrete frames — just continuous interpolation.
 *
 * Technique from Alkemi Games' Drifting Lands, inspired by Prince of Persia (2008).
 * See: http://www.youreallyshouldbeplaying.com/a-game-of-tricks-iii-particles-fun-part1/
 */

#version 300 es
precision mediump float;

/*
 * TEXTURES
 */
uniform sampler2D u_erosionTexture;  // The timing map (see channel breakdown below)
uniform sampler2D u_gradient;        // 2D color ramp, typically 256x64

/*
 * TIMING UNIFORMS
 *
 *   u_animationDuration - Total length of the effect (seconds)
 *   u_fadeInDuration    - How long the "reveal" phase lasts
 *   u_fadeOutDuration   - How long the "dissolve" phase lasts
 *   u_time              - Current playback time (0 to u_animationDuration)
 *
 * The fade-out phase begins at (u_animationDuration - u_fadeOutDuration).
 * Fade-in and fade-out can overlap if their durations exceed half the total.
 */
uniform float u_fadeInDuration;
uniform float u_fadeOutDuration;
uniform float u_animationDuration;
uniform float u_time;

#define u_fadeOutStart (u_animationDuration - u_fadeOutDuration)

in vec2 v_texCoord;
out vec4 fragColor;

// Inverse lerp: returns where 'v' falls between 'a' and 'b' (0.0 to 1.0)
float unlerp(float a, float b, float v) {
    return (v - a) / (b - a);
}

vec4 GetColorADS(vec2 f_life, vec2 texCoord)
{
    /*
     * EROSION TEXTURE CHANNELS
     *
     *   Red   = Reveal order.   0 (dark) appears first, 255 (bright) appears last.
     *   Green = Dissolve order. 0 (dark) vanishes first, 255 (bright) lingers longest.
     *   Blue  = Edge softness.  0 = hard/crisp edge, 255 = soft/feathered edge.
     *
     * Think of R and G as "when does this pixel's note start and stop playing?"
     * The B channel controls whether that note has a sharp attack or a soft swell.
     */
    vec4 texEffect = texture(u_erosionTexture, texCoord);

    /*
     * PIANO KEY TIMING
     *
     * Each pixel is like a piano key with its own press and release time.
     *   key_press   = when this pixel starts appearing (based on red channel)
     *   key_release = when this pixel finishes disappearing (based on green channel)
     *
     * Dark red pixels (texEffect.r ≈ 0) have key_press near u_fadeInDuration (late).
     * Bright red pixels (texEffect.r ≈ 1) have key_press near 0 (early).
     * (Yes, it's inverted — bright reveals first in this convention.)
     */
    float key_press   = u_fadeInDuration * (1.0 - texEffect.r);
    float key_release = u_fadeOutStart + texEffect.g * u_fadeOutDuration;

    // Where are we in this pixel's personal lifetime? (0 = just appeared, 1 = about to vanish)
    float normalized_time = clamp(unlerp(key_press, key_release, u_time), 0.0, 1.0);

    // Flip for OpenGL coordinates (bottom = 0). Remove this line for HLSL/DirectX.
    normalized_time = 1.0 - normalized_time;

    /*
     * GRADIENT RAMP LOOKUP
     *
     *   X axis = position in the effect (from red channel — early vs late pixels)
     *   Y axis = this pixel's current lifetime (just appeared vs fully mature)
     *
     * This lets you do things like:
     *   - Early pixels are yellow, late pixels are grey (fire center vs smoke edges)
     *   - Newly-revealed pixels glow bright, mature pixels are solid/dim
     */
    vec2 rampUV = vec2(
        (1.0 - texEffect.r),   // X: where in the reveal order
        (normalized_time));    // Y: how far through this pixel's life

    vec4 texBLEND = texture(u_gradient, rampUV);

    /*
     * OPACITY CALCULATION (The "Levels" Trick)
     *
     * This is like Photoshop's Levels adjustment, animated over time.
     * Imagine sweeping a threshold across the image — pixels cross the threshold
     * and transition from invisible to visible (or vice versa).
     *
     *   f_life.r = how far through the fade-in phase (0 to 1+)
     *   f_life.g = how far through the fade-out phase (0 to 1+)
     *
     * fadeInFactor:  positive when the sweep has passed this pixel's reveal point
     * fadeOutFactor: positive when this pixel hasn't yet been dissolved away
     */
    float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
    float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

    /*
     * EDGE SOFTNESS
     *
     * The blue channel controls transition sharpness. This multiplier (15.0 * ...)
     * stretches the 0-1 transition over a shorter or longer range:
     *   - Blue ≈ 0: multiplier is ~15, transition happens in ~7% of the range (crisp)
     *   - Blue ≈ 1: multiplier is ~0.15, transition is gradual (soft/feathered)
     *
     * The 1.01 prevents division issues when blue = 1.0 exactly.
     */
    fadeInFactor = clamp(fadeInFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);
    fadeOutFactor = clamp(fadeOutFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);

    // Final color: RGB from gradient, alpha = gradient alpha * fade factors
    return vec4(texBLEND.rgb, texBLEND.a * fadeInFactor * fadeOutFactor);
}

void main()
{
    // Calculate fade progress (could move to vertex shader for optimization)
    vec2 f_life;
    f_life.r = u_time / u_fadeInDuration;                      // Fade-in progress
    f_life.g = (u_time - u_fadeOutStart) / u_fadeOutDuration;  // Fade-out progress

    fragColor = GetColorADS(f_life, v_texCoord);
}
