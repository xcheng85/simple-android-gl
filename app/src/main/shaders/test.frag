#version 450

// Input colour coming from the vertex shader
layout (location = 0) in vec2 inUV;

layout (set = 0, binding = 1) uniform sampler2D samplerColor;

// Output colour for the fragment
layout(location = 0) out vec4 outColor;

void main() {
    outColor = texture(samplerColor, inUV);
}