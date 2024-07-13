#version 450

// vao
layout (location = 0) in vec3 inPos;
layout (location = 1) in vec2 inUV;
layout (location = 2) in vec3 inNormal;

layout (set = 0, binding = 0) uniform UBO
{
	mat4 projection;
	mat4 model;
	mat4 mvp;
	// camera
	vec3 viewPos;
	float lodBias;
} ubo;

layout (location = 0) out vec2 outUV;
layout (location = 1) out float outLodBias;
layout (location = 2) out vec3 outNormalW;
layout (location = 3) out vec3 outViewVec;
layout (location = 4) out vec3 outLightVecW;

out gl_PerVertex
{
    vec4 gl_Position;
};

vec2 positions[3] = vec2[](
    vec2(0.0, -0.5),
    vec2(0.5, 0.5),
    vec2(-0.5, 0.5)
);


void main()
{
	outUV = inUV;
	outLodBias = ubo.lodBias;

    // clip space
    // skip view
	gl_Position = ubo.mvp * vec4(inPos, 1.0);
	//gl_Position = vec4(positions[gl_VertexIndex], 0.0, 1.0);


    vec3 worldPos = vec3(ubo.model * vec4(inPos, 1.0));
	vec3 lightPos = vec3(0.0);
	vec3 lightPosW = mat3(ubo.model) * lightPos;

    // page 323: Introduction to 3D Game Programming with Directx12
    // L vector
    outLightVecW = lightPosW - worldPos;
    // v  vector
    outViewVec = ubo.viewPos - worldPos;
    // vector transformation only need mat3
    // normal in world space
    outNormalW = mat3(inverse(transpose(ubo.model))) * inNormal;

}
