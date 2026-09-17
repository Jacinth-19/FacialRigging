// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
// GPU path: linear blend skinning (4 influences) + blendshape deltas from a texture buffer.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in ivec4 inBoneIDs;
layout(location = 3) in vec4 inWeights;

#define MAX_BONES 32
#define MAX_SHAPES 32

uniform mat4 u_Model;
uniform mat4 u_View;
uniform mat4 u_Proj;
uniform mat4 u_BoneMatrices[MAX_BONES];
uniform int  u_BoneCount;
uniform int  u_ShapeCount;
uniform int  u_VertexCount;
uniform float u_BlendWeights[MAX_SHAPES];
uniform sampler2D u_ShapeDeltas;      // RGB32F 2D texture, vec3 per (shape, vertex): index = shape * vertexCount + vertex
uniform int  u_ShapeTexWidth;
uniform int  u_UseCpuPositions;        // 1 -> positions already deformed on CPU (free-form handles), skip GPU deform

out vec3 vNormal;
out vec3 vWorldPos;

void main() {
    vec3 pos = inPos;
    if (u_UseCpuPositions == 0) {
        // Skinning
        if (u_BoneCount > 0) {
            mat4 skin = inWeights.x * u_BoneMatrices[inBoneIDs.x]
                      + inWeights.y * u_BoneMatrices[inBoneIDs.y]
                      + inWeights.z * u_BoneMatrices[inBoneIDs.z]
                      + inWeights.w * u_BoneMatrices[inBoneIDs.w];
            pos = (skin * vec4(pos, 1.0)).xyz;
        }
        // Blendshapes
        for (int s = 0; s < u_ShapeCount; ++s) {
            float w = u_BlendWeights[s];
            if (abs(w) < 1e-5) continue;
            int idx = s * u_VertexCount + gl_VertexID;
            pos += w * texelFetch(u_ShapeDeltas, ivec2(idx % u_ShapeTexWidth, idx / u_ShapeTexWidth), 0).xyz;
        }
    }
    vec4 world = u_Model * vec4(pos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(u_Model) * inNormal;
    gl_Position = u_Proj * u_View * world;
}
