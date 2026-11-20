#version 330 core
in vec2 Uv;
in vec3 Position;
in float ParticleLife;
out vec4 FragColor;
uniform vec3 particleColor = vec3(0.94,0.85,0.55);
uniform float opacity = 0.75;
uniform vec3 viewPos;
uniform bool fogEnabled;
uniform vec3 fogColor;
uniform float fogDensity;
void main() {
    float coverage = (1.0-smoothstep(0.12,0.5,length(Uv-0.5))) * clamp(opacity,0.0,1.0) * ParticleLife;
    const float bayer[16] = float[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    ivec2 p = ivec2(gl_FragCoord.xy) % 4;
    if (coverage <= (bayer[p.y*4+p.x]+0.5)/16.0) discard;
    float fog = fogEnabled ? 1.0-exp(-fogDensity*length(viewPos-Position)) : 0.0;
    FragColor = vec4(mix(particleColor,fogColor,clamp(fog,0.0,1.0)),1.0);
}
