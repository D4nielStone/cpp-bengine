#version 330 core
in vec2 uv;
out vec4 color;
uniform sampler2D sceneColor;
uniform vec2 viewportSize;
uniform vec2 ps1Resolution = vec2(512.0,288.0);
uniform float ps1ColorLevels = 32.0;
uniform float ps1DitherStrength = 0.35;
void main() {
    vec2 size=max(vec2(1.0),min(viewportSize,ps1Resolution));
    vec2 pixel=floor(uv*size);
    vec3 source=texture(sceneColor,(pixel+0.5)/size).rgb;
    const float bayer[16]=float[16](0,8,2,10,12,4,14,6,3,11,1,9,15,7,13,5);
    ivec2 cell=ivec2(pixel)%4;
    float levels=max(2.0,ps1ColorLevels);
    float dither=(bayer[cell.y*4+cell.x]/16.0-0.5)*clamp(ps1DitherStrength,0.0,1.0)/levels;
    color=vec4(clamp(floor((source+dither)*levels+0.5)/levels,0.0,1.0),1.0);
}
