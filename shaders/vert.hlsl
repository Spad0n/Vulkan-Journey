struct Vertex {
    float3 position;
    float3 color;
};

// Équivalent à votre DrawData GLSL
struct DrawData {
    float4x4 transform;
    uint64_t vBufferPtr; // Adresse mémoire vers le tableau de Vertex
};

struct PushConstants {
    uint64_t drawDataAddr;
};

// Définition de la Push Constant
[[vk::push_constant]]
PushConstants pc;

struct VSOutput {
    float4 position : SV_Position;
    float3 color    : COLOR0;
};

VSOutput main(uint vertexID : SV_VertexID) {
    VSOutput output;

    DrawData root = vk::RawBufferLoad<DrawData>(pc.drawDataAddr, 16U);

    // Équivalent de : Vertex v = root.vBufferPtr.vertices[gl_VertexIndex];
    // On calcule l'adresse exacte du sommet : base + (index * taille)
    uint64_t vertexAddr = root.vBufferPtr + (vertexID * sizeof(Vertex));
    
    // Chargement de la donnée depuis l'adresse mémoire (Device Buffer Address)
    Vertex v = vk::RawBufferLoad<Vertex>(vertexAddr, 8U);

    // Multiplication de matrice (Attention : HLSL utilise mul(M, V) pour l'équivalent GLSL M * V)
    output.position = mul(root.transform, float4(v.position, 1.0f));
    output.color = v.color;

    return output;
}
