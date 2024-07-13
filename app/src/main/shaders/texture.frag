#version 450

layout (set = 0, binding = 1) uniform sampler2D samplerColor;

layout (location = 0) in vec2 inUV;
layout (location = 1) in float inLodBias;
layout (location = 2) in vec3 inNormalW;
layout (location = 3) in vec3 inViewVec;
layout (location = 4) in vec3 inLightVecW;

layout (location = 0) out vec4 outFragColor;

void main()
{
    // level-of-detail computation, mipmap
	vec4 color = texture(samplerColor, inUV, inLodBias);

	vec3 N = normalize(inNormalW);
	vec3 L = normalize(inLightVecW);
	vec3 V = normalize(inViewVec);
	// I vector is -L.
	vec3 R = reflect(-L, N);
	// lambert's cosine law, p326
	vec3 diffuseAlbedo = vec3(1.0);
	vec3 diffuse = max(dot(N, L), 0.0) * diffuseAlbedo;

	float specular = pow(max(dot(R, V), 0.0), 16.0) * color.a;

	outFragColor = vec4(diffuse * color.rgb + specular, 1.0);

}