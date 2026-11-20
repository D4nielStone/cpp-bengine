#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneDepth;
uniform sampler2D occlusionMask;
uniform mat4 inverseProjection;
uniform float radius;
uniform float strength;

float viewDepth(float depth) {
    vec4 position = inverseProjection * vec4(0.0, 0.0, depth * 2.0 - 1.0, 1.0);
    return position.z / position.w;
}

void main() {
    ivec2 size = textureSize(sceneDepth, 0);
    ivec2 pixel = ivec2(uv * vec2(size));
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    if (depth >= 1.0) discard;
    float center = viewDepth(depth);
    float sum = 0.0, weights = 0.0;
    for (int y = -2; y <= 2; ++y) {
        for (int x = -2; x <= 2; ++x) {
            ivec2 point = pixel + ivec2(x, y);
            if (any(lessThan(point, ivec2(0))) || any(greaterThanEqual(point, size))) continue;
            float sampleDepth = texelFetch(sceneDepth, point, 0).r;
            if (sampleDepth >= 1.0) continue;
            float separation = (viewDepth(sampleDepth) - center) / max(radius * 0.12, 0.001);
            float weight = exp(-0.25 * float(x*x + y*y) - separation * separation);
            weights += weight;
            sum += weight * texelFetch(occlusionMask, point, 0).r;
        }
    }
    FragColor = vec4(vec3(mix(1.0, sum / weights, strength)), 1.0);
}
