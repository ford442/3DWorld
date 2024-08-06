#define NUM_CASCADES 4

layout(triangles, invocations = 4) in;
layout(triangle_strip, max_vertices = 3) out;

uniform mat4 light_space_matrices[NUM_CASCADES];

void main() {
    for (int i = 0; i < 3; ++i) {
        gl_Position = light_space_matrices[gl_InvocationID] * gl_in[i].gl_Position;
        gl_Layer = gl_InvocationID;
        EmitVertex();
    }
    EndPrimitive();
}
