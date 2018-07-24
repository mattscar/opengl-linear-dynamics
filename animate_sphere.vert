#version 330

// Identifies the coordinates and normals
in vec3 in_coords;
in vec3 in_normals;

// The output normal from the vertex
out vec3 vertex_normal;

uniform mat4 mvp;     // Modelview-projection matrix
uniform vec3 delta;

// The actual shader function
void main(void) {
  vertex_normal = in_normals;
  gl_Position = mvp * vec4(in_coords + delta, 1.0);
}
