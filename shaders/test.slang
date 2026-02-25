struct __PushConstants_Graphics {
    uint64_t vertData;
    uint64_t fragData;
    uint64_t indirectData;
};

[[vk::push_constant]] __PushConstants_Graphics __push_graphics;

public struct GraphicsPipelineData<TVertexData = void> {
    public property vert : TVertexData* {
        [ForceInline]
        get { return (TVertexData*)__push_graphics.vertData; }
    }
}

struct Vertex
{
    float4 pos;
    float4 color;
}

struct VertexData
{
    Vertex* verts;
}

struct VertexOutput
{
    float4 pos : SV_Position;
    float4 color : TEXCOORD0;
}

[shader("vertex")]
VertexOutput vertexMain(uint vert_id : SV_VertexID, GraphicsPipelineData<VertexData> data)
{
    VertexOutput vert_out;
    vert_out.pos = data.vert.verts[vert_id].pos;
    vert_out.color = data.vert.verts[vert_id].color;
    return vert_out;
}

[shader("fragment")]
float4 fragmentMain(float4 color : TEXCOORD0) : SV_Target
{
    return color;
}
