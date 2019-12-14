in vec2 in_pos;

uniform vec4 dim_pos;

void main()
{
	gl_Position = vec4(in_pos * dim_pos.xy + dim_pos.zw, 0.5, 1);
}
