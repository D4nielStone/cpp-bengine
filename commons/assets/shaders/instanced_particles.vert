#version 330 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec2 aUV;
uniform mat4 model, view, projection;
uniform bool instance;
uniform mat4 transformacoes[128];
uniform float engineTime;
uniform float particleLifetime = 12.0;
uniform float particleSpread = 0.7;
uniform float particleHeight = 1.0;
out vec2 Uv;
out vec3 Position;
out float ParticleLife;
float randomValue(vec2 p) { return fract(sin(dot(p,vec2(12.9898,78.233)))*43758.5453); }
vec2 noiseGradient(vec2 cell) {
    float angle = fract(sin(dot(cell,vec2(127.1,311.7)))*43758.5453)*6.2831853;
    return vec2(cos(angle),sin(angle));
}
float perlinNoise(vec2 point) {
    vec2 cell=floor(point),p=fract(point);
    vec2 fade=p*p*p*(p*(p*6.0-15.0)+10.0);
    float a=dot(noiseGradient(cell),p);
    float b=dot(noiseGradient(cell+vec2(1,0)),p-vec2(1,0));
    float c=dot(noiseGradient(cell+vec2(0,1)),p-vec2(0,1));
    float d=dot(noiseGradient(cell+vec2(1,1)),p-vec2(1,1));
    return mix(mix(a,b,fade.x),mix(c,d,fade.x),fade.y)*1.41421356;
}

void main() {
    mat4 pose=instance?transformacoes[gl_InstanceID]:model;
    vec3 origin=vec3(pose*vec4(0,0,0,1));
    float quad=floor(float(gl_VertexID)/4.0);
    vec2 key=origin.xz+origin.y*vec2(0.73,0.31)+quad*vec2(7.13,19.73);
    float seed=randomValue(key);
    float lifetime=max(1.0,particleLifetime)*(0.8+0.4*seed);
    float age=fract(engineTime/lifetime+seed);
    vec2 drift=vec2(perlinNoise(key*0.2+engineTime*0.06),perlinNoise(key*0.3-engineTime*0.05));
    origin+=vec3((randomValue(key+1.0)-0.5)*particleSpread+drift.x*0.3,
                 (age-0.5)*max(particleHeight,0.0),
                 (randomValue(key+2.0)-0.5)*particleSpread+drift.y*0.3);
    vec3 right=vec3(view[0][0],view[1][0],view[2][0]);
    vec3 up=vec3(view[0][1],view[1][1],view[2][1]);
    float variation=0.7+0.6*randomValue(key+3.0);
    Position=origin+right*aPos.x*length(pose[0].xyz)*variation+up*aPos.y*length(pose[1].xyz)*variation;
    Uv=aUV;
    ParticleLife=smoothstep(0.0,0.15,age)*(1.0-smoothstep(0.75,1.0,age));
    gl_Position=projection*view*vec4(Position,1);
}
