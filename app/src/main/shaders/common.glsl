
#ifndef COMMON_GLSL
#define COMMON_GLSL

struct Vertex {
    float posX;
    float posY;
    float posZ;
    float uvX;
    float uvY;
    int materialId;
};

struct IndirectDrawDef1 {
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int vertexOffset;
    uint firstInstance;
    uint meshId;
    int materialIndex;
};

// 32 bit alignment
struct Material {
    int basecolorTextureId;
    int basecolorSamplerId;
    int metallicRoughnessTextureId;
    int padding;
    vec4 basecolor;
};


#endif
