#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D sceneColor;
uniform float fadeAmount = 0.0;
void main() {
    vec4 source=texture(sceneColor,uv);
    color=vec4(source.rgb*(1.0-clamp(fadeAmount,0.0,1.0)),source.a);
}
