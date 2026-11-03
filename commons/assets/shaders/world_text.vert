#version 330 core
layout(location = 0) in vec4 vertex;
uniform mat4 mvp;
out float shading;

void main() {
    shading = vertex.w;
    gl_Position = mvp * vec4(vertex.xyz, 1.0);
}
