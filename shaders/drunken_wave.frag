uniform sampler2D frame_buffer_tex;
uniform float time      = 0.0;
uniform float intensity = 1.0;
uniform float pos_scale = 1.0;
uniform vec4 color_mod  = vec4(1.0);

in vec2 tc;

void main() {
	vec2 pos     = tc;
	pos.x       += 0.01*intensity*sin(pos_scale*(20.0*pos.x + 10.0*pos.y) + 0.1*time);
	fg_FragColor = color_mod*vec4(texture(frame_buffer_tex, pos).rgb, 1.0);
}
