in vec2 in_pos;
in vec4 in_tex;

uniform vec4 dim_pos;

out vec4 var_tex;

void main()
{
	gl_Position = vec4(in_pos * dim_pos.xy + dim_pos.zw, 0.5, 1);

	var_tex = in_tex;
}
