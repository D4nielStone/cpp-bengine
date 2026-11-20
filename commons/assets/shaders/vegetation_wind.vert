#version 330 core
#define MAX_INSTANCES 128

layout (location = 0) in vec3 aPos;
layout (location = 1) in vec3 aNormal;
layout (location = 2) in vec2 aUV;

out vec3 Normal;
out vec3 Position;
out vec2 Uv;

uniform mat4 view;
uniform mat4 projection;

uniform bool instance;
uniform int instance_id;
uniform mat4 model;
uniform mat4 transformacoes[MAX_INSTANCES];

uniform float engineTime;
uniform float windStrength = 0.3;
uniform vec2 windDirection = vec2(0.8,0.2);
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

void main()
{
    Uv = aUV;

    // Usa transformação por instância se habilitada
    mat4 model_matrix = instance ? transformacoes[gl_InstanceID] : model;

    Normal = mat3(transpose(inverse(model_matrix))) * aNormal;
    Position = vec3(model_matrix * vec4(aPos, 1.0));
    float gust=perlinNoise(Position.xz*0.45+engineTime*vec2(0.16,0.09));
    gust+=0.3*perlinNoise(Position.xz*1.2+engineTime*vec2(0.23,-0.12));
    vec3 bend=vec3(windDirection.x,0,windDirection.y)*gust*windStrength;
    Position+=bend*max(aPos.y,0.0)*length(model_matrix[1].xyz);
    Normal=normalize(Normal-vec3(0,dot(Normal,bend),0));
    gl_Position = projection * view * vec4(Position, 1.0);
}
