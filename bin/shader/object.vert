in vec3 in_pos;
in vec2 in_tex;
in vec3 in_nrm;

in vec4 in_loc;
in vec4 in_mip;
in vec4 in_matr_x;
in vec4 in_matr_y;
in vec4 in_matr_z;
in uint in_sun_dir;

uniform vec4 proj[4];
uniform vec4 proj_shadow[3];
uniform vec4 dim_tex;
uniform vec3 sun_dir[9];
uniform vec3 ambient;

out vec3 var_pos;
out vec4 var_tex;
out vec4 var_loc;
out vec4 var_mip;
out vec4 var_light;

void main()
{
	vec4 pos = vec4(in_pos, 1.0);
	vec4 pos_t = vec4(dot(pos, in_matr_x), dot(pos, in_matr_y), dot(pos, in_matr_z), 1.0);
	gl_Position = vec4(dot(pos_t, proj[0]), dot(pos_t, proj[1]), dot(pos_t, proj[2]), dot(pos_t, proj[3]));

#ifdef SHADOWS
	var_pos = vec3(dot(pos_t, proj_shadow[0]), dot(pos_t, proj_shadow[1]), dot(pos_t, proj_shadow[2]));
#endif
	var_tex = vec4(in_tex, in_mip.zw) * dim_tex;
	var_loc = in_loc;
	var_mip = vec4(in_mip.xy, var_tex.xy * var_loc.zw + var_loc.xy);

	vec3 nrm_t = vec3(dot(in_nrm, in_matr_x.xyz), dot(in_nrm, in_matr_y.xyz), dot(in_nrm, in_matr_z.xyz));
	nrm_t.z = sqrt(1.0 - dot(nrm_t.xy, nrm_t.xy));
	float lca = max(dot(nrm_t, sun_dir[in_sun_dir]), 0.0);
	var_light = vec4(ambient.x, ambient.y, ambient.x + ambient.z, lca);
}
