// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
in vec3 vNormal;
in vec3 vWorldPos;
in float vHeat;
in float vMaterial;
out vec4 FragColor;

uniform vec3 u_CameraPos;
uniform vec3 u_BaseColor;
uniform int  u_Wireframe;
uniform int  u_ShadeMode;
uniform vec3 u_KeyDir;        // main light direction (towards light, world)
uniform vec3 u_KeyColor;      // linear radiance
uniform float u_Exposure;
uniform float u_SssAmount;    // 0..1 wrap / bleed strength
uniform float u_IblAmount;
uniform int  u_ToneMap;       // 0 gamma only, 1 ACES

// Blue -> cyan -> green -> yellow -> red (Maya-style weight ramp).
vec3 heatRamp(float t) {
    t = clamp(t, 0.0, 1.0);
    vec3 c0 = vec3(0.05, 0.05, 0.55), c1 = vec3(0.0, 0.75, 0.95), c2 = vec3(0.1, 0.85, 0.2), c3 = vec3(1.0, 0.9, 0.1), c4 = vec3(0.95, 0.1, 0.05);
    if (t < 0.25) return mix(c0, c1, t / 0.25);
    if (t < 0.5)  return mix(c1, c2, (t - 0.25) / 0.25);
    if (t < 0.75) return mix(c2, c3, (t - 0.5) / 0.25);
    return mix(c3, c4, (t - 0.75) / 0.25);
}

// ---------------------------------------------------------------- PBR-ish skin (mode 5)
const float PI = 3.14159265;
// Analytic studio environment: warm sky dome, cool ground, soft rim - evaluated per direction so it
// doubles as diffuse irradiance (Lambert-convolved approximation) and blurry specular.
vec3 envRadiance(vec3 d, float rough) {
    float up = d.y * 0.5 + 0.5;
    vec3 sky = mix(vec3(0.16, 0.17, 0.22), vec3(0.62, 0.70, 0.85), pow(up, 0.8));
    vec3 ground = vec3(0.20, 0.17, 0.14);
    vec3 env = mix(ground, sky, smoothstep(0.35, 0.65, up));
    // a soft bright window behind-left and a fill front-right; blurred by roughness
    float win = pow(max(dot(d, normalize(vec3(-0.6, 0.35, -0.7))), 0.0), mix(24.0, 2.0, rough)) * 2.2;
    float fill = pow(max(dot(d, normalize(vec3(0.7, 0.2, 0.7))), 0.0), mix(12.0, 1.5, rough)) * 0.8;
    return env + vec3(1.0, 0.95, 0.9) * win + vec3(0.9, 0.93, 1.0) * fill;
}
vec3 envIrradiance(vec3 n) {
    // cosine-weighted 6-tap approximation of the hemisphere integral
    vec3 t = abs(n.y) < 0.99 ? normalize(cross(n, vec3(0, 1, 0))) : vec3(1, 0, 0);
    vec3 b = cross(n, t);
    vec3 acc = envRadiance(n, 1.0) * 0.5;
    acc += 0.125 * (envRadiance(normalize(n + t), 1.0) + envRadiance(normalize(n - t), 1.0) + envRadiance(normalize(n + b), 1.0) + envRadiance(normalize(n - b), 1.0));
    return acc;
}
float D_GGX(float NoH, float a) { float a2 = a * a; float d = NoH * NoH * (a2 - 1.0) + 1.0; return a2 / (PI * d * d + 1e-6); }
float V_Smith(float NoV, float NoL, float a) { float k = a * 0.5; return 0.25 / ((NoV * (1.0 - k) + k) * (NoL * (1.0 - k) + k) + 1e-5); }
vec3 F_Schlick(float VoH, vec3 f0) { return f0 + (1.0 - f0) * pow(1.0 - VoH, 5.0); }
vec3 envBRDF(vec3 f0, float rough, float NoV) { vec4 c0 = vec4(-1.0, -0.0275, -0.572, 0.022), c1 = vec4(1.0, 0.0425, 1.04, -0.04); vec4 r = rough * c0 + c1; float a004 = min(r.x * r.x, exp2(-9.28 * NoV)) * r.x + r.y; vec2 AB = vec2(-1.04, 1.04) * a004 + r.zw; return f0 * AB.x + AB.y; }
vec3 aces(vec3 x) { return clamp((x * (2.51 * x + 0.03)) / (x * (2.43 * x + 0.59) + 0.14), 0.0, 1.0); }

// Pre-integrated skin scattering (Penner): the red bleed grows as curvature and the terminator soften
vec3 skinDiffuse(float NoL, float wrap) {
    float w = clamp(NoL * (1.0 - wrap) + wrap, 0.0, 1.0);          // wrapped lambert
    float r = smoothstep(-wrap * 1.4, 0.55, NoL);                  // red channel scatters furthest
    float g = smoothstep(-wrap * 0.6, 0.65, NoL);
    float bl = smoothstep(-wrap * 0.2, 0.75, NoL);
    vec3 s = vec3(r, g, bl);
    return mix(vec3(w), s * w / max(w, 1e-3), 0.85);
}

vec3 shadeSkin(vec3 N, vec3 V, int mat) {
    vec3 albedo = u_BaseColor; float rough = 0.55; vec3 f0 = vec3(0.028); float sss = u_SssAmount; float specScale = 1.0;
    if (mat == 1) { albedo = vec3(0.92); rough = 0.06; f0 = vec3(0.04); sss = 0.0; specScale = 1.6; }        // eye: wet cornea
    else if (mat == 2) { albedo = vec3(0.90, 0.88, 0.82); rough = 0.25; f0 = vec3(0.045); sss = 0.25; }     // teeth
    else if (mat == 3) { albedo = vec3(0.70, 0.30, 0.32); rough = 0.35; f0 = vec3(0.04); sss = 0.6; }       // tongue
    else if (mat == 4) { albedo = u_BaseColor * vec3(0.62, 0.50, 0.46); rough = 0.8; f0 = vec3(0.02); sss = 0.3; } // lash / hair cards (untextured: a soft dark-skin tone)
    else if (mat == 5) { albedo = vec3(0.66, 0.32, 0.34); rough = 0.3; f0 = vec3(0.04); sss = 0.6; }        // gums                      // eye shadow / socket
    float NoV = max(dot(N, V), 1e-3);
    vec3 col = vec3(0.0);
    // key + fill + rim as analytic lights
    vec3 Ls[3]; vec3 Cs[3];
    Ls[0] = normalize(u_KeyDir); Cs[0] = u_KeyColor;
    Ls[1] = normalize(vec3(-0.7, 0.15, 0.55)); Cs[1] = vec3(0.30, 0.34, 0.42);
    Ls[2] = normalize(vec3(0.2, 0.5, -0.9)); Cs[2] = vec3(0.55, 0.50, 0.45);
    float wrap = 0.5 * sss;
    for (int i = 0; i < 3; ++i) {
        vec3 L = Ls[i]; float NoLraw = dot(N, L); float NoL = max(NoLraw, 0.0);
        vec3 H = normalize(L + V); float NoH = max(dot(N, H), 0.0), VoH = max(dot(V, H), 0.0);
        vec3 diff = albedo * skinDiffuse(NoLraw, wrap) / PI;
        vec3 F = F_Schlick(VoH, f0);
        // two-lobe spec (skin has a broad sheen + tight oily highlight)
        float a1 = rough * rough, a2 = max(rough * 0.5, 0.03) * max(rough * 0.5, 0.03);
        float spec = (D_GGX(NoH, a1) * V_Smith(NoV, NoL, a1) * 0.7 + D_GGX(NoH, a2) * V_Smith(NoV, NoL, a2) * 0.3) * NoL * specScale;
        col += Cs[i] * (diff * (1.0 - F) * PI * 0.55 + F * spec);
    }
    // IBL
    vec3 R = reflect(-V, N);
    vec3 irr = envIrradiance(N) * u_IblAmount;
    vec3 kS = envBRDF(f0, rough, NoV);
    col += albedo * irr * (1.0 - kS) * 0.5;
    col += envRadiance(R, rough) * kS * u_IblAmount * specScale;
    // ambient red bleed in creases: darker facing-away regions pick up subsurface colour
    if (sss > 0.0) col += albedo * vec3(0.9, 0.25, 0.2) * sss * 0.06 * (1.0 - NoV);
    // eyes: extra tight catch-light from the key and a limbal darkening
    if (mat == 1) {
        // procedural iris/pupil on an untextured eyeball: the eyeball normal's angle to the head's forward axis
        float fwd = dot(N, vec3(0.0, 0.0, 1.0));
        float irisEdge = smoothstep(0.945, 0.955, fwd), pupil = smoothstep(0.988, 0.993, fwd);
        vec3 iris = mix(vec3(0.16, 0.22, 0.30), vec3(0.32, 0.36, 0.30), smoothstep(0.955, 0.99, fwd)) * (0.6 + 0.6 * max(dot(N, Ls[0]), 0.0));
        col = mix(col, iris * u_Exposure, irisEdge); col = mix(col, vec3(0.005), pupil);
        vec3 H = normalize(Ls[0] + V); col += u_KeyColor * pow(max(dot(N, H), 0.0), 900.0) * 3.0;
        col *= mix(0.55, 1.0, smoothstep(0.15, 0.55, NoV));
    }
    col *= u_Exposure;
    vec3 mapped = u_ToneMap == 1 ? aces(col) : clamp(col, 0.0, 1.0);
    return pow(mapped, vec3(1.0 / 2.2));
}

void main() {
    if (u_Wireframe == 1) { FragColor = vec4(0.15, 0.6, 0.9, 0.35); return; }
    if (u_Wireframe == 2) { // eye occlusion shell: soft shadow, strongest at grazing angles / near the lid crease
        vec3 Nn = normalize(vNormal); vec3 Vv = normalize(u_CameraPos - vWorldPos);
        float a = mix(0.10, 0.35, pow(1.0 - max(dot(Nn, Vv), 0.0), 1.5));
        FragColor = vec4(vec3(0.10, 0.06, 0.05), a); return;
    }
    vec3 N = normalize(vNormal);
    vec3 V = normalize(u_CameraPos - vWorldPos);
    if (u_ShadeMode == 1) { FragColor = vec4(N * 0.5 + 0.5, 1.0); return; }
    if (u_ShadeMode == 5) { FragColor = vec4(shadeSkin(N, V, int(vMaterial + 0.5)), 1.0); return; }
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
