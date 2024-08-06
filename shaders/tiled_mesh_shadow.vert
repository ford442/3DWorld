uniform float x1, y1, dx_inv, dy_inv;
uniform float delta_z    = 0.0;
uniform float exclude_dz = 0.0;
uniform vec3 translate   = vec3(0.0);
uniform vec4 exclude_box = vec4(0.0);
uniform sampler2D height_tex;

void main() {
	vec4 vertex = fg_Vertex;
	vertex.z   += texture(height_tex, vec2((vertex.x - x1)*dx_inv, (vertex.y - y1)*dy_inv)).r + delta_z;
	vertex.xyz += translate;
	if (vertex.x > exclude_box.x && vertex.y > exclude_box.y && vertex.x < exclude_box.z && vertex.y < exclude_box.w) {vertex.z += exclude_dz;}
	gl_Position = fg_ModelViewProjectionMatrix * vertex;
} 
