in vec4 var_tex;
in vec4 var_blend;
in vec4 var_color;
in vec4 var_fade;
in float var_lod;

uniform sampler1D pal;
uniform sampler2D checker;
uniform sampler2D recol_pal;
uniform sampler2DArray atlas_c;
uniform sampler2DArray atlas_m;
uniform sampler2D color_c;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec4 frag_map;

void main()
{
	vec4 col = textureLod(atlas_c, var_tex.xyz, var_lod); // 32bit color
	float val_m = textureLod(atlas_m, var_tex.xyz, var_lod).r; // anim/remap/recol pos
	float idx_m = val_m * 255.0; // anim/remap/recol index
	float val_u = clamp(idx_m, 0.0, 1.0); // use anim/remap/recol index?
	float val_r = mix(var_color.b, var_color.a, idx_m - 1.0); // palette pos for text remap
	float val_t = texture(recol_pal, vec2(val_m, var_tex.w)).r; // palette pos for sprite recolor

	float val_p = mix(mix(val_m, val_t, var_blend.w), val_r, var_blend.z); // palette pos
	vec4 m_color = mix(col, texture(pal, val_p), val_u); // sprite color

	float a_fill = mix(1.0, texture(checker, var_tex.xy).r, var_blend.x); // checker fill
	float c_fill = a_fill * var_blend.y; // filling rect?
	vec4 b_color = mix(m_color, var_color, c_fill); // sprite or rect color
	vec4 a_color = vec4(b_color.rgb, b_color.a * a_fill); // checker alpha

	vec4 n_color = mix(a_color, texture(color_c, var_tex.xy), var_fade.z); // black color
	float c = dot(n_color.rgb, vec3(0.299, 0.587, 0.114)) * var_fade.y; // greyed color
	vec4 f_color = vec4(c, c, c, n_color.a * var_fade.a); // transparent color
	frag_color = mix(n_color, f_color, var_fade.x); // final color

	float m = mix(val_p, 0.0, c_fill + var_fade.x); // shift palette pos to <0, if filling a rect or fading
	frag_map = vec4(m, m, m, mix(val_u, c_fill, var_blend.y)); // overwrite palette pos only if used or a rect filling
}
