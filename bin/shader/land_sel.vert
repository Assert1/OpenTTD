in vec3 in_pos;
in vec4 in_tex;
in vec4 in_loc;
in vec2 in_mip;
in vec3 in_nrm;

uniform vec4 proj[4];
uniform vec4 dim_tex;

out vec4 var_tex;
out vec4 var_loc;
out vec4 var_mip;

void main()
{
	vec4 pos = vec4(in_pos, 1.0);
	gl_Position = vec4(dot(pos, proj[0]), dot(pos, proj[1]), dot(pos, proj[2]), dot(pos, proj[3]));

	var_tex = in_tex * dim_tex;
	var_loc = in_loc;
	var_mip = vec4(in_mip, var_tex.xy * var_loc.zw + var_loc.xy);
}
