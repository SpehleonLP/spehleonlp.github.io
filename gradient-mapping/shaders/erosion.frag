#version 300 es
precision mediump float;
uniform sampler2D u_erosionTexture;
uniform sampler2D u_gradient;

uniform float u_fadeInDuration;
uniform float u_fadeOutDuration;
uniform float u_animationDuration;
uniform float u_time;
uniform vec2 u_viewportSize;

#define u_fadeOutStart (u_animationDuration - u_fadeOutDuration)

in vec2 v_texCoord;
in vec2 v_texCoordPx;
in vec3 f_life;

out vec4 fragColor;

float unlerp(float min, float max, float value)
{
	return (value - min) / (max - min);
}

// most intuitive for artists
vec4 GetColorADS(vec2 texCoord)
{
	vec4 texEffect = texture(u_erosionTexture, texCoord);

	float key_press   = u_fadeInDuration * (1.0 - texEffect.r);
	float key_release =  u_fadeOutStart + texEffect.g * u_fadeOutDuration;

	float normalized_time = clamp(unlerp(key_press, key_release, u_time), 0.0, 1.0);
	
	// in openGL the bottom is 0, in HLSL the top is 0.
	normalized_time = 1.0 - normalized_time;
	
// most intuitive for artists
// and makes best use of texels we have access to.
	vec2 rampUV = vec2(
		(1.0 - texEffect.r), 
		(normalized_time));
		
	vec4 texBLEND = texture(u_gradient, rampUV);

// ----

    float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
    float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

	fadeInFactor = clamp(fadeInFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);
	fadeOutFactor = clamp(fadeOutFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);
	
	return vec4(texBLEND.rgb, texBLEND.a * fadeInFactor*fadeOutFactor);
}


void main()
{
	ivec2 cell = ivec2(v_texCoordPx / 8.0);
	int grid = (cell.x & 0x01) ^ (cell.y & 0x01);

	vec4 background = mix(vec4(0.75, 0.75, 0.75, 1.0), vec4(1.0), float(grid));
	vec4 object = vec4(0);

	object = GetColorADS(v_texCoord);

	fragColor = vec4(mix(background.rgb, object.rgb, object.a), 1.0);
}
