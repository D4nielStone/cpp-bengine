#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D sceneColor;
uniform vec2 viewportSize;
uniform float bloomStrength = 0.2;
uniform float bloomThreshold = 0.72;
uniform float bloomRadius = 3.0;
uniform float exposure = 1.0;
uniform float fadeToBlack = 0.0;
uniform vec3 tint = vec3(1.0);
void main() {
    vec3 scene = texture(sceneColor, uv).rgb;
    vec3 glow = vec3(0.0);
    vec2 pixel = max(bloomRadius, 0.0) / max(viewportSize, vec2(1.0));
    for (int x = -2; x <= 2; ++x) {
        for (int y = -2; y <= 2; ++y) {
            vec3 tap = texture(sceneColor, clamp(uv + vec2(x,y) * pixel, vec2(0.0), vec2(1.0))).rgb;
            glow += max(tap - vec3(max(bloomThreshold, 0.0)), vec3(0.0)) / 25.0;
        }
    }
    vec3 graded = (scene + glow * max(bloomStrength, 0.0)) * max(tint, vec3(0.0)) * max(exposure, 0.0);
    color = vec4(graded * (1.0 - clamp(fadeToBlack, 0.0, 1.0)), 1.0);
}
