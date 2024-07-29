#version 460 // gl_BaseVertex and gl_DrawID
#extension GL_EXT_nonuniform_qualifier : require
#extension GL_GOOGLE_include_directive : require

#include "common.glsl"

layout(location = 0) in vec2 inTexCoord;
layout(location = 1) in flat uint inMeshId;

layout(location = 0) out vec4 outFragColor;

void main()
{
  if (inMeshId == 0) {
    outFragColor = vec4(1.0);
  } else {
   outFragColor = vec4(0.0, 1.0, 0.0, 1.0);
  }
}