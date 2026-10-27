#version 330 core
out vec4 FragColor;

in vec2 Uv;
in vec3 Normal;
in vec3 Position;

#define PI 3.14159265359
#define MAX_POINT_LIGHTS 16

struct Material {
    vec4 albedo;
    float metallic;
    float roughness;
    float ao;
};

struct DirLight {
    vec3 direction;
    vec3 color;
    float intensity;
    vec3 ambient;
};

struct PointLight {
    vec3 position;
    vec3 color;
    float intensity;
    float constant;
    float linear;
    float quadratic;
};

uniform Material material;
uniform DirLight dirLight;
uniform PointLight pointLights[MAX_POINT_LIGHTS];
uniform int pointLightCount;
uniform vec3 viewPos;

uniform sampler2D tex_albedo;
uniform sampler2D tex_metallic;
uniform sampler2D tex_roughness;
uniform sampler2D tex_normal;
uniform sampler2D tex_ao;
uniform sampler2D tex_height;

uniform bool use_tex_albedo;
uniform bool use_tex_metallic;
uniform bool use_tex_roughness;
uniform bool use_tex_normal;
uniform bool use_tex_ao;
uniform bool use_tex_height;
uniform bool uvMundo;

uniform bool recebe_luz;

vec3 fresnelSchlick(float cosTheta, vec3 F0) {
    return F0 + (1.0 - F0) * pow(1.0 - cosTheta, 5.0);
}

float DistributionGGX(vec3 N, vec3 H, float roughness) {
    float a = roughness * roughness;
    float a2 = a * a;
    float NdotH = max(dot(N, H), 0.0);
    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0) + 1.0);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness) {
    float r = (roughness + 1.0);
    float k = (r * r) / 8.0;
    return NdotV / (NdotV * (1.0 - k) + k);
}

float GeometrySmith(vec3 N, vec3 V, vec3 L, float roughness) {
    float NdotV = max(dot(N, V), 0.0);
    float NdotL = max(dot(N, L), 0.0);
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);
    return ggx1 * ggx2;
}

vec3 calculateLightLo(vec3 L, vec3 radiance, vec3 N, vec3 V, vec3 albedo, float metallic, float roughness) {
    vec3 H = normalize(V + L);
    vec3 F0 = mix(vec3(0.04), albedo, metallic);
    float NDF = DistributionGGX(N, H, roughness);
    float G = GeometrySmith(N, V, L, roughness);
    vec3 F = fresnelSchlick(max(dot(H, V), 0.0), F0);

    vec3 numerator = NDF * G * F;
    float denominator = 4.0 * max(dot(N, V), 0.0) * max(dot(N, L), 0.0) + 0.001;
    vec3 specular = numerator / denominator;

    vec3 kS = F;
    vec3 kD = (vec3(1.0) - kS) * (1.0 - metallic);
    float NdotL = max(dot(N, L), 0.0);
    return (kD * albedo / PI + specular) * radiance * NdotL;
}

void main() {
    vec2 novoUv = uvMundo ? Position.xz * 0.1 : Uv;

    vec4 albedo = (use_tex_albedo ? texture(tex_albedo, novoUv) : vec4(1.0)) * material.albedo;
    float metallic = clamp(material.metallic * (use_tex_metallic ? texture(tex_metallic, novoUv).r : 1.0), 0.0, 1.0);
    float roughness = clamp(material.roughness * (use_tex_roughness ? texture(tex_roughness, novoUv).r : 1.0), 0.04, 1.0);
    float ao = clamp(material.ao * (use_tex_ao ? texture(tex_ao, novoUv).r : 1.0), 0.0, 1.0);

    vec3 N = dot(Normal, Normal) > 1e-8
        ? normalize(Normal)
        : vec3(0.0, 1.0, 0.0);
    if (use_tex_normal) {
        // Normal maps are tangent-space vectors, not world-space normals.
        vec3 dpdx = dFdx(Position);
        vec3 dpdy = dFdy(Position);
        vec2 duvdx = dFdx(novoUv);
        vec2 duvdy = dFdy(novoUv);
        float determinant = duvdx.x * duvdy.y - duvdx.y * duvdy.x;
        if (abs(determinant) > 1e-8) {
            vec3 T = (dpdx * duvdy.y - dpdy * duvdx.y) / determinant;
            vec3 sourceB = (dpdy * duvdx.x - dpdx * duvdy.x) / determinant;
            T -= N * dot(N, T);
            vec3 mapped = texture(tex_normal, novoUv).rgb * 2.0 - 1.0;
            if (dot(T, T) > 1e-8 && dot(mapped, mapped) > 1e-8) {
                T = normalize(T);
                vec3 B = normalize(cross(N, T));
                B *= dot(B, sourceB) < 0.0 ? -1.0 : 1.0;
                N = normalize(mat3(T, B, N) * mapped);
            }
        }
    }

    vec3 V = normalize(viewPos - Position);
    vec3 result = vec3(0.0);

    vec3 ambient = dirLight.ambient * albedo.rgb * ao * dirLight.intensity;
    result += ambient;

    vec3 L_dir = normalize(-dirLight.direction);
    vec3 radiance_dir = dirLight.color * dirLight.intensity;
    result += calculateLightLo(L_dir, radiance_dir, N, V, albedo.rgb, metallic, roughness);

    for (int i = 0; i < pointLightCount && i < MAX_POINT_LIGHTS; ++i) {
        PointLight light = pointLights[i];
        vec3 L = normalize(light.position - Position);
        float distanceToLight = length(light.position - Position);
        float attenuation = 1.0 / (light.constant + light.linear * distanceToLight + light.quadratic * distanceToLight * distanceToLight);
        vec3 radiance = light.color * light.intensity * attenuation;
        result += calculateLightLo(L, radiance, N, V, albedo.rgb, metallic, roughness);
    }

    float gamma = 2.2;
    FragColor = vec4(pow(result.rgb, vec3(1.0 / gamma)), albedo.a);
}
