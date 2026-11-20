#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D sceneColor;
uniform float exposure = 1.0;
void main() {
    vec4 scene = texture(sceneColor, uv);
    color = vec4(scene.rgb * exposure, scene.a);
}
