#version 330 core
in vec2 ndc;
out vec4 FragColor;
uniform mat4 inverseCamera;
uniform vec3 horizon;
uniform vec3 zenith;
uniform bool useCubemap;
uniform samplerCube sky;
void main() {
    vec4 p = inverseCamera * vec4(ndc, 1.0, 1.0);
    vec3 direction = normalize(p.xyz / p.w);
    float height = pow(clamp(direction.y, 0.0, 1.0), 0.65);
    FragColor = vec4(useCubemap ? texture(sky, direction).rgb : mix(horizon, zenith, height), 1.0);
}
