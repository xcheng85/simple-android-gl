#version 460 // gl_BaseVertex and gl_DrawID
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_EXT_debug_printf : enable
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout (set = 0, binding = 0) uniform UBO
{
	mat4 projection;
	mat4 model;
	mat4 mvp;
	// camera
	vec3 viewPos;
	float lodBias;
} ubo;

layout(set = 3, binding = 0) readonly buffer VertexBuffer {
	Vertex vertices[];
};

layout(set = 2, binding = 0) readonly buffer IndirectDrawBuffer {
	IndirectDrawDef1 indirectDraws[];
};

layout(set = 4, binding = 0) readonly buffer MaterialBuffer {
    Material materials[];
};

// output from vs to fs
// flat: no interpolation
layout(location = 0) out vec2 outTexCoord;
layout(location = 1) out flat uint outMeshId;
layout(location = 2) out flat int outMaterialId;

void main() {
  //gl_Position = ubo.mvp * vec4(inPos, 1.0);
  //debugPrintfEXT("gl_VertexIndex = %d",gl_VertexIndex);

  Vertex vertex = vertices[gl_VertexIndex];

  outTexCoord = vec2(vertex.uvX, vertex.uvY);
  // gl_DrawID: for multi-draw commands
  outMeshId = indirectDraws[gl_DrawID].meshId;
  outMaterialId = vertex.materialId;

  gl_Position = ubo.mvp * vec4(vertex.posX, vertex.posY, vertex.posZ, 1.0f);
}