#version 330 core
in float shading;
uniform vec4 ink;
out vec4 color;

void main() {
    color = vec4(ink.rgb * shading, ink.a);
}
