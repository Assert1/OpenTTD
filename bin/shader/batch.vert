in vec4 in_pos;
in vec4 in_tex;
in vec4 in_blend;
in vec4 in_color;
in vec4 in_fade;

uniform vec4 dim_pos;
uniform vec4 dim_tex;

out vec4 var_tex;
out vec4 var_blend;
out vec4 var_color;
out vec4 var_fade;
out float var_lod;

void main()
{
	gl_Position = vec4(in_pos.xy * dim_pos.xy + dim_pos.zw, in_pos.z, 1.0);

	var_lod = in_pos.w;
	var_tex = in_tex * dim_tex;
	var_blend = in_blend;
	var_color = in_color;
	var_fade = in_fade;
}
