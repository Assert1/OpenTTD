in vec3 in_pos;
in vec4 in_tex;
in vec4 in_loc;

uniform vec4 proj[4];
uniform vec4 dim_tex;

out vec4 var_tex;

void main()
{
	vec4 pos = vec4(in_pos, 1.0);
	gl_Position = vec4(dot(pos, proj[0]), dot(pos, proj[1]), dot(pos, proj[2]), dot(pos, proj[3])); // projected xyz

	vec4 tex = in_tex * dim_tex;
	var_tex = vec4(tex.xy * in_loc.zw + in_loc.xy, tex.zw);
}
