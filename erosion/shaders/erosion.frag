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
	
#if 1
// most intuitive for artists
// and makes best use of texels we have access to.
	vec2 rampUV = vec2(
		(1.0 - texEffect.r), 
		(normalized_time));
#else
// looks best giving existing data files.
// a lot of texels are never read from
	vec2 rampUV = vec2(
		u_time / u_animationDuration, 
		normalized_time);
#endif
	
	vec4 texBLEND = texture(u_gradient, rampUV);

// ----

    float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
    float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

	fadeInFactor = clamp(fadeInFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);
	fadeOutFactor = clamp(fadeOutFactor * 15.0 * (1.01 - texEffect.b), 0.0, 1.0);
	
	return vec4(texBLEND.rgb, texBLEND.a * fadeInFactor*fadeOutFactor);
}

#if 0
// original reference from 2015 used this function
// i hate it because it has 2 dependent reads
// and is basically impossible to reason about becuase of that stupid blend. 
vec4 GetColorOriginal(vec2 texCoord)
{
	vec4 texEffect = texture(u_erosionTexture, texCoord);

    float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
    float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

	// Sample 2D color ramp for fade-in and fade-out
	// Left half (U: 0-0.5) is fade-in, right half (U: 0.5-1.0) is fade-out
	// V coordinate is the fadeInFactor/fadeOutFactor (gradient along visible edge)
	vec2 rampUV_in = vec2(f_life.r * 0.5, 1.0 - fadeInFactor);
	vec2 rampUV_out = vec2(0.5 + f_life.g * 0.5, fadeOutFactor);

	vec4 texIN = texture(u_gradient, rampUV_in);
	vec4 texOUT = texture(u_gradient, rampUV_out);

	// Blend between fade-in and fade-out colors using TransitionRatio
	vec4 texBLEND = mix(texIN, texOUT, vec4(f_life.b));

	fadeInFactor = clamp(fadeInFactor * 15.0 * (1.0 - texEffect.b), 0.0, 1.0);
	fadeOutFactor = clamp(fadeOutFactor * 15.0 * (1.0 - texEffect.b), 0.0, 1.0);

	return vec4(texBLEND.rgb, texBLEND.a * fadeInFactor*fadeOutFactor);
}
#endif

void main()
{
	if(u_viewportSize == vec2(1, 1))
	{
		fragColor = GetColorADS(v_texCoord);
		return;
	}

	ivec2 cell = ivec2(v_texCoordPx / 8.0);
	int grid = (cell.x & 0x01) ^ (cell.y & 0x01);

	vec4 background = mix(vec4(0.75, 0.75, 0.75, 1.0), vec4(1.0), float(grid));
	vec4 object = vec4(0);

	vec2 texCoord = v_texCoord * 1.5;

	if(texCoord.x < 1.0 && texCoord.y < 1.0)
	{
		object = GetColorADS(texCoord);
	}
	else if(texCoord.x < 1.0 && texCoord.y < 2.0)
	{
		texCoord -= floor(texCoord);
		texCoord *= 3.0;

		if(texCoord.y < 1.0)
		{
			vec4 texEffect = texture(u_erosionTexture, texCoord);

			float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
			float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

			if(texCoord.x < 1.0)
			{
				fadeOutFactor = 0.0;
			}
			else if(texCoord.x < 2.0)
			{
				fadeInFactor = 0.0;
			}

			object = vec4(fadeInFactor, fadeOutFactor, 0, 1);
		}
	}


	fragColor = vec4(mix(background.rgb, object.rgb, object.a), 1.0);
}
