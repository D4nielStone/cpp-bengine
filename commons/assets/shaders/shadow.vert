#version 330 core
layout(location = 0) in vec3 aPos;
uniform mat4 model;
uniform mat4 lightSpaceMatrix;
uniform bool instance;
uniform mat4 transformacoes[128];
void main() {
    mat4 pose = instance ? transformacoes[gl_InstanceID] : model;
    gl_Position = lightSpaceMatrix * pose * vec4(aPos, 1.0);
}
