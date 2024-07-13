#version 450

// no vao

layout (location = 0) out vec2 outUV;

// Uniform buffer containing an MVP matrix.
// Currently the vulkan backend only sets the rotation matix
// required to handle device rotation.
layout(set = 0, binding = 0) uniform UniformBufferObject {
    mat4 MVP;
} ubo;

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);

vec2 uvs[3] = vec2[](
    vec2(0.25, 0.25),
    vec2(0.75, 0.75),
    vec2(0.5, 0.5)
);

void main() {
    gl_Position = ubo.MVP * vec4(positions[gl_VertexIndex], 0.0, 1.0);
    outUV = uvs[gl_VertexIndex];
}