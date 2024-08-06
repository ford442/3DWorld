uniform vec4 xlate_scale = vec4(0,0,0,1); // encoded as {tx, ty, tz, scale}

void main() {
	vec4 vertex = vec4(xlate_scale.xyz, 0.0) + (vec4(xlate_scale.www, 1.0) * fg_Vertex);
	gl_Position = fg_ModelViewProjectionMatrix * vertex;
	fg_Color_vf = vec4(0,0,0,1); // black/unused
}
