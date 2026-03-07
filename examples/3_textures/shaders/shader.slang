import "../../shared.slang";

struct Vertex {
    float3 pos;
    float2 uv;
};

struct VertexData {
    Vertex *verts;
};

struct VertexOutput {
    float4 pos : SV_Position;
    float2 uv : UV;
};

struct FragmentData {
    uint texture_a;
    uint texture_b;
    uint samplerIdx;
    float fade;
};

[shader("vertex")]
VertexOutput vertexMain(uint vert_id : SV_VertexID, GraphicsPipelineData<VertexData> data) {
    VertexOutput vert_out;
    vert_out.pos = float4(data.vert.verts[vert_id].pos, 1.0);
    vert_out.uv = data.vert.verts[vert_id].uv;
    return vert_out;
}

[shader("fragment")]
float4 fragmentMain(float2 uv : UV, GraphicsPipelineData<void, FragmentData> data) : SV_Target {
    float4 tex_a = sampleTexture(data.frag.texture_a, data.frag.samplerIdx, uv);
    float4 tex_b = sampleTexture(data.frag.texture_b, data.frag.samplerIdx, uv);
    return lerp(tex_a, tex_b, data.frag.fade);
}

