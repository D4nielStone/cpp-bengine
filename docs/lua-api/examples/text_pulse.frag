// BGUI acrescenta #version 330 core e o bloco BguiMaterial.
in vec2 TexCoord;
out vec4 FragColor;
uniform sampler2D tex;
uniform float hudTime;
void main() {
    float coverage = texture(tex, TexCoord).r;
    float pulse = 0.65 + 0.35 * sin(hudTime * 3.0);
    FragColor = vec4(text_color.rgb * pulse, text_color.a * coverage);
}
