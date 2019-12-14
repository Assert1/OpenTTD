in vec4 var_tex;
in vec4 var_loc;

uniform sampler2D recol_pal;
uniform sampler2DArray atlas_m;

layout(location = 0) out vec4 frag_color;

void main()
{
	vec4 col = texture(atlas_m, var_tex.xyz); // texture color + recolor, using nearest filtering for the alpha test
	float map = col.a; // texture recolor pos

	float idx_m = map * 255.0; // recolor index
	float val_u = clamp(255.0 - idx_m, 0.0, 1.0); // use recolor index?
	float val_t = texture(recol_pal, vec2(map, var_tex.w)).x; // palette pos for recolor

	float m_alpha = mix(col.a, val_t, val_u); // alpha
	if (m_alpha <= 0.0) discard;

	frag_color = vec4(0.0, 0.0, 0.0, 1.0); // color fill
}
