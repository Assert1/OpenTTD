in vec3 in_pos;
in vec2 in_tex;
in vec3 in_nrm;

in vec4 in_loc;
in vec4 in_mip;
in vec4 in_matr_x;
in vec4 in_matr_y;
in vec4 in_matr_z;

uniform vec4 proj[4];
uniform vec4 dim_tex;

out vec4 var_tex;

void main()
{
	vec4 pos = vec4(in_pos, 1.0);
	vec4 pos_t = vec4(dot(pos, in_matr_x), dot(pos, in_matr_y), dot(pos, in_matr_z), 1.0); // actual xyz
	gl_Position = vec4(dot(pos_t, proj[0]), dot(pos_t, proj[1]), dot(pos_t, proj[2]), dot(pos_t, proj[3])); // projected xyz

	vec4 tex = vec4(in_tex, in_mip.zw) * dim_tex;
	var_tex = vec4(tex.xy * in_loc.zw + in_loc.xy, tex.zw);
}
