in vec4 var_tex;

uniform sampler1D pal;
uniform sampler2D screen_c;
uniform sampler2D screen_m;

layout(location = 0) out vec4 frag_color;

void main()
{
	vec4 col = texture(screen_c, var_tex.xy); // 32bit color
	float map = texture(screen_m, var_tex.xy).r; // palette pos

	float idx_z = map * 255.0; // palette index
	float idx_c = clamp(idx_z, 0.0, 1.0); // use palette?
	frag_color = mix(col, texture(pal, map), idx_c); // final color
}
