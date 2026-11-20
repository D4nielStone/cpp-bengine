#version 330 core
in vec2 Uv;
in float ParticleLife;
out vec4 FragColor;

uniform sampler2D sceneDepth;
uniform bool hasSceneDepth;
uniform vec2 viewportOrigin;
uniform mat4 inverseProjection;
uniform vec3 particleColor = vec3(0.87, 0.66, 0.3);
uniform float opacity = 0.45;
uniform float softDistance = 0.18;

float viewDepth(float depth) {
    vec4 position = inverseProjection * vec4(0.0, 0.0, depth * 2.0 - 1.0, 1.0);
    return -position.z / position.w;
}

void main() {
    float coverage = (1.0 - smoothstep(0.02, 0.5, length(Uv - 0.5))) * ParticleLife;
    if (hasSceneDepth) {
        ivec2 pixel = ivec2(gl_FragCoord.xy - viewportOrigin);
        float opaqueDepth = texelFetch(sceneDepth, pixel, 0).r;
        float gap = viewDepth(opaqueDepth) - viewDepth(gl_FragCoord.z);
        coverage *= smoothstep(0.0, max(softDistance, 0.001), gap);
    }
    FragColor = vec4(particleColor, coverage * clamp(opacity, 0.0, 1.0));
}
