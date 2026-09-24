#version 330 core

in vec2 Uv;
out vec4 FragColor;

uniform sampler2D texture;
uniform vec2 texture_resolution;
uniform bool flip = false;
uniform float time;           // para animação da granulação
uniform float sepiaStrength = 0.1;  // intensity do tom sépia (0.0–1.0)
uniform float vignetteSize = 0.75;   // size da vinheta (0.0–1.5)
uniform float vignetteSoft = 0.5;   // suavidade da vinheta (0.0–1.0)
uniform float noiseStrength = 0.05;  // intensity da granulação (0.0–0.1)

// Quantiza uma color em n-levels por channel
vec3 quantize8bit(vec3 color) {
    // 3 bits red, 3 bits green, 2 bits blue => levels = (8, 8, 4)
    vec3 levels = vec3(8.0, 8.0, 4.0);
    return floor(color * levels) / levels;
}

float rand(vec2 co) {
    // geração de ruído simples
    return fract(sin(dot(co.xy ,vec2(12.9898,78.233))) * 43758.5453);
}

void main() {
    // Amostra de color original
    vec2 uv = Uv;
    if(flip) uv.y = -uv.y;
    vec4 color = texture(texture, uv);

    // --- TONALIDADE SÉPIA ---
    float r = dot(color.rgb, vec3(0.393, 0.769, 0.189));
    float g = dot(color.rgb, vec3(0.349, 0.686, 0.168));
    float b = dot(color.rgb, vec3(0.272, 0.534, 0.131));
    vec3 sepia = vec3(r, g, b);
    color.rgb = mix(color.rgb, sepia, sepiaStrength);

    // --- VINHETA ---
    vec2 position = (Uv * texture_resolution - 0.5 * texture_resolution) / texture_resolution;
    float len = length(position);
    float vignette = smoothstep(vignetteSize, vignetteSize - vignetteSoft, len);
    color.rgb *= vignette;

    // --- GRANULAÇÃO ---
    float noise = rand(Uv * time) * noiseStrength;
    color.rgb += noise;

    // --- 8‑BIT QUANTIZATION ---
    color.rgb = quantize8bit(color.rgb);

    FragColor = vec4(color.rgb, color.a);
}
