#version 330 core
layout(location=0) in vec3 aPos;
layout(location=2) in vec2 aUV;
uniform mat4 model, view, projection;
uniform bool instance;
uniform mat4 transformacoes[128];
out vec2 Uv;
out vec3 Position;
out float ParticleLife;
void main() {
    mat4 pose = instance ? transformacoes[gl_InstanceID] : model;
    vec3 origin = vec3(pose * vec4(0,0,0,1));
    vec3 right = vec3(view[0][0], view[1][0], view[2][0]);
    vec3 up = vec3(view[0][1], view[1][1], view[2][1]);
    Position = origin + right*aPos.x*length(pose[0].xyz) + up*aPos.y*length(pose[1].xyz);
    Uv = aUV;
    ParticleLife = 1.0;
    gl_Position = projection*view*vec4(Position,1);
}
