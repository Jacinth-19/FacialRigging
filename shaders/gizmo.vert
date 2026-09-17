// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec4 inColor;
uniform mat4 u_ViewProj;
uniform float u_PointSize;
out vec4 vColor;
void main() { vColor = inColor; gl_Position = u_ViewProj * vec4(inPos, 1.0); gl_PointSize = u_PointSize; }
