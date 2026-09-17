// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
in vec3 vNormal;
in vec3 vWorldPos;
out vec4 FragColor;

uniform vec3 u_CameraPos;
uniform vec3 u_BaseColor;
uniform int  u_Wireframe;

void main() {
    if (u_Wireframe == 1) { FragColor = vec4(0.15, 0.6, 0.9, 0.35); return; }
    vec3 N = normalize(vNormal);
    vec3 V = normalize(u_CameraPos - vWorldPos);
    vec3 L1 = normalize(vec3(0.5, 0.8, 0.9));
    vec3 L2 = normalize(vec3(-0.7, 0.2, 0.4));
    float diff = max(dot(N, L1), 0.0) * 0.85 + max(dot(N, L2), 0.0) * 0.35;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.25;
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.25;
    vec3 c = u_BaseColor * (0.18 + diff) + spec + rim * vec3(0.4, 0.5, 0.7);
    FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
