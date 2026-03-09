[[vk::binding(0, 0)]] public Texture2D textures[];
[[vk::binding(0, 1)]] public SamplerState samplers[];

public struct __PushConstants_Graphics {
    public uint64_t vertData;
    public uint64_t fragData;
    public uint64_t indirectData;
};

[[vk::push_constant]] __PushConstants_Graphics __push_graphics;

// Standard Vulkan indirect draw command structure
public struct DrawIndexedIndirectCommand
{
    uint indexCount;
    uint instanceCount;
    uint firstIndex;
    int  vertexOffset;
    uint firstInstance;
};

struct __DrawCommand<TUserData>
{
    DrawIndexedIndirectCommand cmd;
    TUserData data;
};

in int draw_id : SV_DrawIndex;

public struct GraphicsPipelineData<TVertexData = void, TFragmentData = void, TDrawData = void> {
    public property vert : TVertexData* {
        [ForceInline]
        get { return (TVertexData*)__push_graphics.vertData; }
    }

    public property frag : TFragmentData* {
        [ForceInline]
        get { return (TFragmentData*)__push_graphics.fragData; }
    }

    public property drawData : TDrawData {
        [ForceInline]
        get { 
            __DrawCommand<TDrawData>* buffer = (__DrawCommand<TDrawData>*)__push_graphics.indirectData;
            return buffer[draw_id].data; 
        }
    }

    public property drawCmd : DrawIndexedIndirectCommand {
        [ForceInline]
        get { 
            __DrawCommand<TDrawData>* buffer = (__DrawCommand<TDrawData>*)__push_graphics.indirectData;
            return buffer[draw_id].cmd; 
        }
    }
};

public float4 sampleTexture(uint textureIndex, uint samplerIndex, float2 uv) {
    return textures[NonUniformResourceIndex(textureIndex)]
        .Sample(samplers[NonUniformResourceIndex(samplerIndex)], uv);
}
