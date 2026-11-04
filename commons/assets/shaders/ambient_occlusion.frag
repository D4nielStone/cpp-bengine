#version 330 core

in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneDepth;
uniform vec2 viewportSize;
uniform mat4 projection;
uniform mat4 inverseProjection;
uniform int mode;
uniform float radius;
uniform float strength;

const float PI = 3.14159265359;
const int STEPS = 6;
const int SLICES = 6;

bool inBounds(ivec2 pixel, ivec2 size) {
    return all(greaterThanEqual(pixel, ivec2(0))) && all(lessThan(pixel, size));
}

vec3 viewPosition(ivec2 pixel, ivec2 size, float depth) {
    vec2 ndc = (vec2(pixel) + 0.5) / vec2(size) * 2.0 - 1.0;
    vec4 position = inverseProjection * vec4(ndc, depth * 2.0 - 1.0, 1.0);
    return position.xyz / position.w;
}

vec3 neighborDelta(ivec2 pixel, ivec2 offset, ivec2 size, vec3 center) {
    ivec2 neighbor = pixel + offset;
    if (!inBounds(neighbor, size)) return vec3(0.0);
    float depth = texelFetch(sceneDepth, neighbor, 0).r;
    if (depth >= 1.0) return vec3(0.0);
    return viewPosition(neighbor, size, depth) - center;
}

vec3 surfaceTangent(ivec2 pixel, ivec2 axis, ivec2 size, vec3 center) {
    vec3 forward = neighborDelta(pixel, axis, size, center);
    vec3 backward = -neighborDelta(pixel, -axis, size, center);
    if (forward == vec3(0.0)) return backward;
    if (backward == vec3(0.0)) return forward;
    return abs(forward.z) < abs(backward.z) ? forward : backward;
}

// Integral of cos(theta - normalAngle) * sin(theta) on one side of a GTAO slice.
float arcIntegral(float theta, float normalAngle) {
    float s = sin(theta);
    return 0.5 * cos(normalAngle) * s * s +
        sin(normalAngle) * (0.5 * theta - 0.25 * sin(2.0 * theta));
}

void main() {
    ivec2 size = ivec2(viewportSize);
    ivec2 pixel = ivec2(uv * vec2(size));
    float depth = texelFetch(sceneDepth, pixel, 0).r;
    if (depth >= 1.0) discard;

    vec3 center = viewPosition(pixel, size, depth);
    vec3 normal = cross(surfaceTangent(pixel, ivec2(1, 0), size, center),
                        surfaceTangent(pixel, ivec2(0, 1), size, center));
    if (dot(normal, normal) < 1e-20) discard;
    normal = normalize(normal);
    bool perspective = projection[3][3] == 0.0;
    vec3 eye = perspective ? normalize(-center) : vec3(0.0, 0.0, 1.0);
    if (dot(normal, eye) < 0.0) normal = -normal;
    float clipW = perspective ? -center.z : 1.0;
    vec2 pixelRadius = 0.5 * radius * vec2(size) *
        vec2(projection[0][0], projection[1][1]) / clipW;

    float occlusion = 0.0;
    float reference = 0.0;
    for (int slice = 0; slice < SLICES; ++slice) {
        float angle = PI * float(slice) / float(SLICES);
        vec2 direction = vec2(cos(angle), sin(angle));
        vec3 axis = vec3(direction, 0.0);
        axis = normalize(axis - eye * dot(axis, eye));
        for (int side = -1; side <= 1; side += 2) {
            float normalAngle = atan(dot(normal, axis) * float(side), max(dot(normal, eye), 1e-5));
            float tangent = normalAngle + 0.5 * PI;
            float horizon = cos(tangent);
            vec2 ray = direction * float(side) * pixelRadius;
            ray *= min(1.0, 64.0 / max(length(ray), 1.0));
            ivec2 previous = pixel;
            for (int stepIndex = 1; stepIndex <= STEPS; ++stepIndex) {
                float fraction = float(stepIndex) / float(STEPS);
                ivec2 samplePixel = pixel + ivec2(round(ray * fraction * fraction));
                if (samplePixel == previous) continue;
                previous = samplePixel;
                vec3 delta = neighborDelta(pixel, samplePixel - pixel, size, center);
                float distanceToSample = length(delta);
                if (distanceToSample < 1e-5 || distanceToSample >= radius) continue;
                float weight = 1.0 - smoothstep(0.0, radius, distanceToSample);
                float elevation = dot(normal, delta);
                float bias = radius * 0.025;
                if (elevation <= bias) continue;

                if (mode == 1) {
                    occlusion += weight * (elevation - bias) / distanceToSample;
                } else {
                    vec3 biasedDelta = delta - normal * bias;
                    float slope = dot(normalize(biasedDelta), eye);
                    horizon = max(horizon, mix(cos(tangent), slope, weight));
                }
            }

            if (mode == 2) {
                occlusion += horizon - cos(tangent);
            } else if (mode == 3) {
                float projectedNormal = length(vec2(dot(normal, eye), dot(normal, axis)));
                float baseline = arcIntegral(tangent, normalAngle);
                float visible = arcIntegral(acos(clamp(horizon, -1.0, 1.0)), normalAngle);
                occlusion += projectedNormal * max(baseline - visible, 0.0);
                reference += projectedNormal * baseline;
            }
        }
    }

    float normalization = mode == 3 ? max(reference, 1e-5) :
        float(2 * SLICES) * (mode == 1 ? float(STEPS) : 1.0);
    FragColor = vec4(vec3(1.0 - strength * clamp(occlusion / normalization, 0.0, 1.0)), 1.0);
}
