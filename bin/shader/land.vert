in vec3 in_pos;
in vec4 in_tex;
in vec4 in_loc;
in vec2 in_mip;
in vec3 in_nrm;

uniform vec4 proj[4];
uniform vec4 proj_shadow[3];
uniform vec4 dim_tex;
uniform vec3 sun_dir;
uniform vec3 ambient;

out vec3 var_pos;
out vec4 var_tex;
out vec4 var_loc;
out vec4 var_mip;
out vec2 var_light;

void main()
{
	vec4 pos = vec4(in_pos, 1.0);
	gl_Position = vec4(dot(pos, proj[0]), dot(pos, proj[1]), dot(pos, proj[2]), dot(pos, proj[3])); // projected xyz

#ifdef SHADOWS
	var_pos = vec3(dot(pos, proj_shadow[0]), dot(pos, proj_shadow[1]), dot(pos, proj_shadow[2])); // shadow space xyz
#endif
	var_tex = in_tex * dim_tex;
	var_loc = in_loc;
	var_mip = vec4(in_mip, var_tex.xy * var_loc.zw + var_loc.xy);
	float light = (dot(in_nrm, sun_dir.xyz) - ambient.x) * ambient.y + ambient.x; // rebalanced light intensity
	var_light = vec2(light, ambient.z); // light intensity and shift
}
