uniform int num_lights=0;

in vec3 normal;
in vec4 epos;

void main()
{
	vec3 n     = normalize(normal);
	vec3 color = vec3(0.0);

	for (int i = 0; i < num_lights; ++i) { // sun_diffuse, galaxy_ambient, dynamic ...
		color += add_pt_light_comp(n, epos, i).rgb;
	}
	fg_FragColor = apply_fog_epos(vec4(color, 1.0), epos); // apply standard fog
}

