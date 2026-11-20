#version 330 core
in vec2 uv;
out vec4 FragColor;

uniform sampler2D sceneColor;
uniform sampler2D sceneDepth;
uniform sampler2D directionalShadowMap;
uniform bool hasSceneDepth;
uniform bool hasDirectionalShadow;
uniform mat4 inverseViewProjection;
uniform mat4 viewProjection;
uniform mat4 directionalLightSpace;
uniform vec3 sunDirection;
uniform vec3 sunColor;
uniform float sunIntensity;
uniform float volumeDensity = 0.047;
uniform float volumeDistance = 34.0;
uniform float volumeHeight = 5.0;
uniform float volumeBase = 0.0;
uniform vec3 volumeTint = vec3(0.62, 0.68, 0.57);
uniform float shaftStrength = 0.85;
uniform float flareStrength = 0.35;

vec3 worldPosition(vec2 screen, float depth) {
    vec4 p = inverseViewProjection * vec4(screen * 2.0 - 1.0, depth * 2.0 - 1.0, 1.0);
    return p.xyz / p.w;
}

float sunlight(vec3 point) {
    if (!hasDirectionalShadow) return 1.0;
    vec4 p = directionalLightSpace * vec4(point, 1.0);
    vec3 coord = p.xyz / p.w * 0.5 + 0.5;
    if (any(lessThan(coord, vec3(0.0))) || any(greaterThan(coord, vec3(1.0)))) return 1.0;
    return float(coord.z - 0.00035 <= texture(directionalShadowMap, coord.xy).r);
}

float radialFlare(vec3 toSun, vec2 dimensions) {
    vec4 clip = viewProjection * vec4(toSun, 0.0);
    if (clip.w <= 0.0 || flareStrength <= 0.0) return 0.0;
    vec2 light = clip.xy / clip.w * 0.5 + 0.5;
    if (any(lessThanEqual(light, vec2(0.0))) || any(greaterThanEqual(light, vec2(1.0)))) return 0.0;
    vec2 aspect = vec2(dimensions.x / dimensions.y, 1.0);
    float visible = 0.0;
    for (int y = -2; y <= 2; ++y)
        for (int x = -2; x <= 2; ++x)
            visible += step(0.999999, texture(sceneDepth, light + vec2(x,y) * 0.003 / aspect).r);
    vec2 d = (uv - light) * aspect;
    float r = length(d);
    float angle = atan(d.y, d.x + 0.000001);
    float glow = exp(-r*r*210.0) + 0.24*exp(-r*6.0);
    float streaks = pow(abs(cos(angle*3.0)), 18.0) * exp(-r*13.0) * 0.28;
    float ghost = exp(-length((uv - (0.5 - (light - 0.5)*0.54))*aspect)*27.0) * 0.13;
    float edge = smoothstep(0.0, 0.065, min(min(light.x, light.y), min(1.0-light.x, 1.0-light.y)));
    return flareStrength * edge * (visible / 25.0) * (glow + streaks + ghost);
}

void main() {
    vec4 scene = texture(sceneColor, uv);
    if (!hasSceneDepth) { FragColor = scene; return; }

    float depth = texture(sceneDepth, uv).r;
    vec3 start = worldPosition(uv, 0.0);
    vec3 segment = worldPosition(uv, depth) - start;
    float distanceToSurface = length(segment);
    vec3 ray = segment / max(distanceToSurface, 0.00001);
    vec3 toSun = -sunDirection / max(length(sunDirection), 0.00001);
    float forwardScatter = 0.20 + 1.35 * pow(max(dot(ray, toSun), 0.0), 6.0);
    float sun = clamp(sunIntensity * 0.42, 0.0, 2.0);
    float stepSize = min(distanceToSurface, max(volumeDistance, 0.0)) / 24.0;
    float jitter = fract(dot(mod(gl_FragCoord.xy, 8.0), vec2(0.75487766, 0.56984029)));
    float transmission = 1.0;
    vec3 scatter = vec3(0.0);
    for (int i = 0; i < 24; ++i) {
        vec3 point = start + ray * ((float(i) + jitter) * stepSize);
        float above = max(point.y - volumeBase, 0.0);
        float density = max(volumeDensity, 0.0) * exp(-above / max(volumeHeight, 0.1));
        float stepTransmission = exp(-density * stepSize);
        vec3 lighting = volumeTint * 0.51 +
            sunColor * (sun * forwardScatter * max(shaftStrength, 0.0) * sunlight(point));
        scatter += transmission * (1.0 - stepTransmission) * lighting;
        transmission *= stepTransmission;
        if (transmission < 0.005) break;
    }
    vec3 flare = sunColor * sun * radialFlare(toSun, vec2(textureSize(sceneDepth, 0)));
    FragColor = vec4(scene.rgb * transmission + scatter + flare, scene.a);
}
