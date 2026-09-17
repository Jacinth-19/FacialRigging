// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeat;
out vec4 FragColor;

uniform vec3 u_CameraPos;
uniform vec3 u_BaseColor;
uniform int  u_Wireframe;
uniform int  u_ShadeMode;

// Blue -> cyan -> green -> yellow -> red (Maya-style weight ramp).
vec3 heatRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.05, 0.05, 0.55), c1 = vec3(0.0, 0.75, 0.95), c2 = vec3(0.1, 0.85, 0.2), c3 = vec3(1.0, 0.9, 0.1), c4 = vec3(0.95, 0.1, 0.05);
    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.5)  return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.5) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}

void main() {
    if (u_Wireframe == 1) { FragColor = vec4(0.15, 0.6, 0.9, 0.35); return; }
    vec3 N = normalize(vNormal);
    vec3 V = normalize(u_CameraPos - vWorldPos);
    if (u_ShadeMode == 1) { FragColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (u_ShadeMode >= 2) {
        float shade = 0.55 + 0.45 * max(dot(N, normalize(vec3(0.5, 0.8, 0.9))), 0.0);
        vec3 c = mix(vec3(0.42), heatRamp(vHeat), vHeat > 0.002 ? 1.0 : 0.0) * shade;
        FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0); return;
    }
    vec3 L1 = normalize(vec3(0.5, 0.8, 0.9));
    vec3 L2 = normalize(vec3(-0.7, 0.2, 0.4));
    float diff = max(dot(N, L1), 0.0) * 0.85 + max(dot(N, L2), 0.0) * 0.35;
    float rim = pow(1.0 - max(dot(N, V), 0.0), 3.0) * 0.25;
    vec3 H = normalize(L1 + V);
    float spec = pow(max(dot(N, H), 0.0), 32.0) * 0.25;
    vec3 c = u_BaseColor * (0.18 + diff) + spec + rim * vec3(0.4, 0.5, 0.7);
    FragColor = vec4(pow(c, vec3(1.0 / 2.2)), 1.0);
}
