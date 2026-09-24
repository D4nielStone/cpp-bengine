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

void main()
{
    Uv = aUV;

    // Usa transformação por instância se habilitada
    mat4 model_matrix = instance ? transformacoes[instance_id] : model;

    Normal = mat3(transpose(inverse(model_matrix))) * aNormal;
    Position = vec3(model_matrix * vec4(aPos, 1.0));
    gl_Position = projection * view * model_matrix * vec4(aPos, 1.0);
}
