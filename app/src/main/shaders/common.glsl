
#ifndef COMMON_GLSL
#define COMMON_GLSL


struct Vertex {
    float posX;
    float posY;
    float posZ;
    float uvX;
    float uvY;
    int material;
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

#endif
