in vec3 var_pos;
in vec4 var_tex;
in vec4 var_loc;
in vec4 var_mip;
in vec2 var_light;

uniform sampler2D pal;
uniform sampler2D recol_pal;
uniform sampler2DArray atlas_c;
uniform sampler2DArray atlas_m;
uniform sampler2DShadow shadow;

layout(location = 0) out vec4 frag_color;
layout(location = 1) out vec4 frag_map;

vec2 atlas_edge_bound(vec2 tex)
{
#ifdef _GL_VERSION_3_3
	vec2 size = vec2(textureSize(atlas_c, 0).xy);

	vec2 dxt = dFdx(tex) * size;
    vec2 dyt = dFdy(tex) * size;
    float dmax_sqr = max(dot(dxt, dxt), dot(dyt, dyt));

	float k = (255.0 / (2.0 * size.x)); // assume square atlas
	float p = (dmax_sqr + 4.0) / (4.0 * 256.0) - 1e-5;
	float b = texture(recol_pal, vec2(p, 1.0)).r * k;
	return vec2(b, b);
#else
	vec2 lod = textureQueryLOD(atlas_c, tex);
	int lod_max = int(ceil(max(lod.x, 0.0)) + 0.5);
	ivec2 size = textureSize(atlas_c, lod_max).xy;
	return vec2(0.5, 0.5) / size;
#endif
}

void main()
{
	vec2 eb = atlas_edge_bound(var_mip.zw);
	vec2 lo = eb.xy * var_mip.xy; // lo tex coord limit
	vec2 hi = vec2(1.0, 1.0) - lo; // hi tex coord limit
	vec2 tex = clamp(var_tex.xy, lo, hi); // atlas clipping

	vec3 tex_atlas = vec3(tex.xy * var_loc.zw + var_loc.xy, var_tex.z); // atlas tex coords
	vec4 col = texture(atlas_c, tex_atlas); // texture color
	float map = texture(atlas_m, tex_atlas).a; // texture recolor pos

	float idx_m = map * 255.0; // recolor index
	float val_u = clamp(255.0 - idx_m, 0.0, 1.0); // use recolor index?
	float val_t = texture(recol_pal, vec2(map, var_tex.w)).x; // palette pos for recolor

	vec4 m_color = mix(col, texture(pal, vec2(val_t, 0.5)), val_u); // color
	if (m_color.a <= 0.0) discard;

#ifdef SHADOWS
	float dim = texture(shadow, var_pos) * 0.5 + 0.5; // shadow dim
	float light = var_light.x * dim;
#else
	float light = var_light.x;
#endif
	frag_color = vec4(m_color.rgb * (light + var_light.y), 1.0); // shaded color
	frag_map = vec4(0.0, 0.0, 0.0, 1.0);
}
