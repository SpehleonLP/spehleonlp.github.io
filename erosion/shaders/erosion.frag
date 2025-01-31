#version 300 es
precision mediump float;
precision mediump sampler3D;
uniform sampler2D u_erosionTexture;
uniform sampler2D u_gradient;
uniform sampler3D u_gradient3D;

uniform float u_has3DGradient;

uniform float u_fadeInDuration;
uniform float u_fadeOutDuration;
uniform float u_animationDuration;
uniform float u_time;
uniform vec2 u_viewportSize;

#define u_fadeOutStart (u_animationDuration - u_fadeOutDuration)

in vec2 v_texCoord;
in vec2 v_texCoordPx;
in vec3 f_life;
flat in float textureRatio;

out vec4 fragColor;


vec4 GetColorOriginal(vec2 texCoord)
{
	vec4 texEffect = texture(u_erosionTexture, texCoord);

    float fadeInFactor = clamp(f_life.r - (1.0 - texEffect.r), 0.0, 1.0);
    float fadeOutFactor = clamp(texEffect.g - f_life.g, 0.0, 1.0);

	float fadeInStart   = (1.0 - texEffect.r) * u_fadeInDuration;
	float fadeOutEnd    = texEffect.g * u_fadeOutDuration + u_fadeOutStart;
	float fadeProgress  = (u_time - fadeInStart) / (fadeOutEnd - fadeInStart);

	vec4 texBLEND = vec4(texEffect.rg, fadeProgress, 1.0);

	if(u_has3DGradient != 0.0)
	{
		texBLEND = texture(u_gradient3D, vec3(texEffect.rg, fadeProgress));
	}
	else
	{
		vec2 rampUV_in = vec2(f_life.r * 0.5, 1.0 - fadeInFactor);
		vec2 rampUV_out = vec2((0.5f + f_life.g * 0.5), fadeOutFactor);

		vec4 texIN = texture(u_gradient, rampUV_in);
		vec4 texOUT= texture(u_gradient, rampUV_out);

		texBLEND = mix(texIN, texOUT, vec4(f_life.b));
	}

	fadeInFactor = clamp(fadeInFactor * 15.0 * (1.0 - texEffect.b), 0.0, 1.0);
	fadeOutFactor = clamp(fadeOutFactor * 15.0 * (1.0 - texEffect.b), 0.0, 1.0);

	return vec4(texBLEND.rgb, texBLEND.a * fadeInFactor*fadeOutFactor);
}

void main()
{
	if(u_viewportSize == vec2(1, 1))
	{
		fragColor = GetColorOriginal(v_texCoord);
		return;
	}

	ivec2 cell = ivec2(v_texCoordPx / 8.0);
	int grid = (cell.x & 0x01) ^ (cell.y & 0x01);

	vec4 background = mix(vec4(0.75, 0.75, 0.75, 1.0), vec4(1.0), float(grid));
	vec4 object = vec4(0);

	vec2 texCoord = v_texCoord * 1.5;

	if(texCoord.x < 1.0 && texCoord.y < 1.0)
	{
		object = GetColorOriginal(texCoord * 2.0);
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
