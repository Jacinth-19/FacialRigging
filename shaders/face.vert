// No #version here: the app prepends '#version 330 core' or '#version 300 es' (+precision).
// GPU path: linear blend skinning (4 influences) + blendshape deltas from a texture buffer.
layout(location = 0) in vec3 inPos;
layout(location = 1) in vec3 inNormal;
layout(location = 2) in ivec4 inBoneIDs;
layout(location = 3) in vec4 inWeights;
layout(location = 4) in float inMaterial;   // 0 skin, 1 eye, 2 teeth, 3 tongue, 4 hair (brows/lashes), 5 gums, 6 eye shadow

#define MAX_BONES 32
#define MAX_SHAPES 80

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
uniform int  u_ShadeMode;              // 0 lit, 1 normals, 2 bone-weight heatmap, 3 blendshape influence, 4 displacement
uniform int  u_HeatBone;               // bone index for mode 2
uniform int  u_HeatShape;              // shape index for mode 3 (-1 = all active shapes)
uniform float u_HeatScale;             // displacement -> [0,1] scale for modes 3/4

out vec3 vNormal;
out vec3 vWorldPos;
out float vHeat;
out float vMaterial;

void main() {
    vec3 pos = inPos;
    vHeat = 0.0;
    vMaterial = inMaterial;
    if (u_ShadeMode == 2) {
        vHeat = (inBoneIDs.x == u_HeatBone ? inWeights.x : 0.0) + (inBoneIDs.y == u_HeatBone ? inWeights.y : 0.0)
              + (inBoneIDs.z == u_HeatBone ? inWeights.z : 0.0) + (inBoneIDs.w == u_HeatBone ? inWeights.w : 0.0);
    } else if (u_ShadeMode == 3) {
        vec3 d = vec3(0.0);
        for (int s = 0; s < u_ShapeCount; ++s) {
            if (u_HeatShape >= 0 ? s != u_HeatShape : abs(u_BlendWeights[s]) < 1e-5) continue;
            int idx = s * u_VertexCount + gl_VertexID;
            d += (u_HeatShape >= 0 ? 1.0 : u_BlendWeights[s]) * texelFetch(u_ShapeDeltas, ivec2(idx % u_ShapeTexWidth, idx / u_ShapeTexWidth), 0).xyz;
        }
        vHeat = clamp(length(d) * u_HeatScale, 0.0, 1.0);
    }
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
    if (u_ShadeMode == 4) vHeat = clamp(length(pos - inPos) * u_HeatScale, 0.0, 1.0);
    vec4 world = u_Model * vec4(pos, 1.0);
    vWorldPos = world.xyz;
    vNormal = mat3(u_Model) * inNormal;
    gl_Position = u_Proj * u_View * world;
}
