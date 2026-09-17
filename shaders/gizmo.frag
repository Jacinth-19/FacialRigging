#version 330 core
in vec4 vColor;
out vec4 FragColor;
uniform int u_Round;
void main() {
    if (u_Round == 1) {
        vec2 d = gl_PointCoord - vec2(0.5);
        float r = dot(d, d);
        if (r > 0.25) discard;
        float edge = smoothstep(0.25, 0.16, r);
        FragColor = vec4(vColor.rgb * (0.6 + 0.4 * edge), vColor.a);
    } else FragColor = vColor;
}
