in vec2 var_tex;

uniform sampler2D pixels;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec4 frag_map;

void main()
{
	frag_color = textureLod(pixels, var_tex.xy, 0.0);
	frag_map = vec4(0.0, 0.0, 0.0, 1.0);
}
