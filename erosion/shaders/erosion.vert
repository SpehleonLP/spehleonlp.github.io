#version 300 es
precision mediump float;
uniform sampler2D u_erosionTexture;
uniform sampler2D u_gradient;

uniform vec2 u_viewportSize;
uniform float u_fadeInDuration;
uniform float u_fadeOutDuration;
uniform float u_transitionDuration;
uniform float u_animationDuration;
uniform float u_time;

#define u_fadeOutStart (u_animationDuration - u_fadeOutDuration)

in vec2 a_position;
in vec2 a_texCoord;

out vec2 v_texCoord;
out vec2 v_texCoordPx;
out vec3 f_life;


void main()
{
	gl_Position = vec4(a_position, 0, 1);
	v_texCoord = a_texCoord * u_viewportSize / min(u_viewportSize.x, u_viewportSize.y);
	v_texCoordPx = a_texCoord * u_viewportSize;

	f_life.r = u_time / u_fadeInDuration;
	f_life.g = (u_time - u_fadeOutStart) / u_fadeOutDuration;

	// TransitionRatio: blend between fade-in and fade-out colors (matches Alkemi)
	// Transition starts when fade-in completes, transitions over FadeTransitionDuration
	float FadeOffsetDuration = u_fadeOutStart - u_fadeInDuration;
	float FadeTransitionStart = u_fadeInDuration + FadeOffsetDuration * 0.5;
	f_life.b = (u_time - FadeTransitionStart) / u_transitionDuration;

	f_life = clamp(f_life, vec3(0), vec3(1));

	if(u_viewportSize.x == 1.0)
	{
		v_texCoord.y = 1.0 - v_texCoord.y;
	}
}
