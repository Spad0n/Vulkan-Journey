[[vk::binding(0, 0)]] public Texture2D textures[];
[[vk::binding(0, 1)]] public SamplerState samplers[];

public struct __PushConstants_Graphics {
    public uint64_t vertData;
    public uint64_t fragData;
    public uint64_t indirectData;
};

[[vk::push_constant]] __PushConstants_Graphics __push_graphics;

public struct GraphicsPipelineData<TVertexData = void, TFragmentData = void, TDrawData = void> {
    public property vert : TVertexData* {
        [ForceInline]
        get { return (TVertexData*)__push_graphics.vertData; }
    }

    public property frag : TFragmentData* {
        [ForceInline]
        get { return (TFragmentData*)__push_graphics.fragData; }
    }
};

public float4 sampleTexture(uint textureIndex, uint samplerIndex, float2 uv) {
    return textures[NonUniformResourceIndex(textureIndex)]
        .Sample(samplers[NonUniformResourceIndex(samplerIndex)], uv);
}
