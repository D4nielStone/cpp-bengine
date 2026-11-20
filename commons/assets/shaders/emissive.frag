#version 330 core
out vec4 FragColor;
uniform vec3 emissionColor = vec3(0.5, 1.0, 0.8);
uniform float emissionStrength = 1.0;
void main() {
    FragColor = vec4(max(emissionColor, vec3(0.0)) * max(emissionStrength, 0.0), 1.0);
}
