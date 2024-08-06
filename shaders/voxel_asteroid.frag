uniform int num_lights=0;
uniform sampler3D ao_tex, shadow_tex;

//in vec3 vpos, eye_norm, normal; // come from bump_map.part.frag and triplanar_bump_map.part.frag
//in vec4 epos; // comes from bump_map.part.frag

void main()
{
	vec3 n     = normalize(eye_norm);
	vec4 texel = get_texture_val(normalize(normal), vpos);
	vec3 color = vec3(0.0);
	vec3 tc    = 0.5*(vpos.zxy + 1.0);
	float light_scales[8] = float[](1,1,1,1,1,1,1,1);
	light_scales[0] = texture(shadow_tex, tc).r; // diffuse/shadow term
	light_scales[1] = texture(ao_tex,     tc).r; // ambient term

	for (int i = 0; i < num_lights; ++i) { // sun_diffuse, galaxy_ambient, dynamic ...
		color += light_scales[i]*add_pt_light_comp(n, epos, i).rgb;
	}
	fg_FragColor = vec4(texel.rgb * clamp(color, 0.0, 1.0), texel.a * gl_Color.a); // use gl_Color alpha directly
}

