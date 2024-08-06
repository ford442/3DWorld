uniform sampler2D color_map, normal_map;
uniform vec3 ref_dir     = vec3(0,1,0);
uniform vec4 color_scale = vec4(1.0);
uniform vec3 camera_pos;

in vec4 world_space_pos, eye_space_pos;
in vec2 tc;

void main() {
	vec4 texel = texture(color_map, tc);
	if (texel.a < 0.5) discard; // transparent
	check_noise_and_maybe_discard(0.0, gl_Color.a);

	// transform normal into billboard orientation 
	vec3 normal = 2.0*texture(normal_map, tc).xyz - vec3(1.0);
	normal.y *= -1.0; // texture is rendered with ybot < ytop
	vec3 vdir = camera_pos - world_space_pos.xyz;
	vec2 rd_n = normalize(ref_dir.xy);
	vec2 vd_n = normalize(vdir.xy);
	float dp  = dot(rd_n, vd_n);
	float s   = length(cross(vec3(rd_n, 0), vec3(vd_n, 0))) * ((cross(vdir, ref_dir).z < 0.0) ? -1.0 : 1.0);
	mat3 mrot = mat3(dp, -s, 0.0,  s, dp, 0.0,  0.0, 0.0, 1.0);
	normal    = normalize(fg_NormalMatrix * (mrot * normal)); // convert to eye space
	
	vec3 color  = vec3(0.0);
	if (enable_light0) color += add_light_comp0(normal).rgb;
	if (enable_light1) color += add_light_comp1(normal).rgb;
	if (enable_light2) color += add_light_comp (normal, 2).rgb * calc_light_atten(eye_space_pos, 2);
	fg_FragColor = apply_fog_epos(vec4(clamp(color*color_scale.rgb, 0.0, 1.0)*texel.rgb, 1.0), eye_space_pos);
}
