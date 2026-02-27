
static VkFormat toVkTextureFormat(TextureFormat type) {
    switch (type) {
    case TextureFormat::RGBA8_Unorm: return VK_FORMAT_R8G8B8A8_UNORM;
    case TextureFormat::BGRA8_Unorm: return VK_FORMAT_B8G8R8A8_UNORM;
    case TextureFormat::RGBA8_SRGB: return VK_FORMAT_R8G8B8A8_SRGB;
    case TextureFormat::BGRA8_SRGB: return VK_FORMAT_B8G8R8A8_SRGB;
    case TextureFormat::D32_Float: return VK_FORMAT_D32_SFLOAT;
    case TextureFormat::RGBA16_Float: return VK_FORMAT_R16G16B16A16_SFLOAT;
    case TextureFormat::RGBA32_Float: return VK_FORMAT_R32G32B32A32_SFLOAT;
    case TextureFormat::BC1_RGBA_Unorm: return VK_FORMAT_BC1_RGBA_UNORM_BLOCK;
    case TextureFormat::BC3_RGBA_Unorm: return VK_FORMAT_BC3_UNORM_BLOCK;
    case TextureFormat::BC7_RGBA_Unorm: return VK_FORMAT_BC7_UNORM_BLOCK;
    case TextureFormat::ASTC_4x4_RGBA_Unorm: return VK_FORMAT_ASTC_4x4_UNORM_BLOCK;
    case TextureFormat::ETC2_RGB8_Unorm: return VK_FORMAT_ETC2_R8G8B8_UNORM_BLOCK;
    case TextureFormat::ETC2_RGBA8_Unorm: return VK_FORMAT_ETC2_R8G8B8A8_UNORM_BLOCK;
    case TextureFormat::EAC_R11_Unorm: return VK_FORMAT_EAC_R11_UNORM_BLOCK;
    case TextureFormat::EAC_RG11_Unorm: return VK_FORMAT_EAC_R11G11_UNORM_BLOCK;
    default:
        fprintf(stderr, "Implementation bug\n");
        exit(1);
    }
}

static VkPrimitiveTopology toVkTopology(PrimitiveTopology type) {
    switch (type) {
    case PrimitiveTopology::TriangleList:  return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    case PrimitiveTopology::TriangleStrip: return VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP;
    case PrimitiveTopology::LineList:      return VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
    case PrimitiveTopology::LineStrip:     return VK_PRIMITIVE_TOPOLOGY_LINE_STRIP;
    case PrimitiveTopology::PointList:     return VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
    default: assert(0 && "Unreachable");
    }
}

static VkFrontFace toVkFrontFace(FrontFace type) {
    switch (type) {
    case FrontFace::CounterClockwise: return VK_FRONT_FACE_COUNTER_CLOCKWISE;
    case FrontFace::Clockwise: return VK_FRONT_FACE_CLOCKWISE;
    default: assert(0 && "Unreachable");
    }
}

static VkCullModeFlags toVkCullMode(CullMode type) {
    switch (type) {
    case CullMode::None: return VK_CULL_MODE_NONE;
    case CullMode::Front: return VK_CULL_MODE_FRONT_BIT;
    case CullMode::Back: return VK_CULL_MODE_BACK_BIT;
    case CullMode::FrontAndBack: return VK_CULL_MODE_FRONT_AND_BACK;
    default: assert(0 && "Unreachable");
    }
}

static VkCompareOp toVkCompareOp(CompareOp type) {
    switch (type) {
    case CompareOp::Never: return VK_COMPARE_OP_NEVER;
    case CompareOp::Less: return VK_COMPARE_OP_LESS;
    case CompareOp::Equal: return VK_COMPARE_OP_EQUAL;
    case CompareOp::LessEqual: return VK_COMPARE_OP_LESS_OR_EQUAL;
    case CompareOp::Greater: return VK_COMPARE_OP_GREATER;
    case CompareOp::NotEqual: return VK_COMPARE_OP_NOT_EQUAL;
    case CompareOp::GreaterEqual: return VK_COMPARE_OP_GREATER_OR_EQUAL;
    case CompareOp::Always: return VK_COMPARE_OP_ALWAYS;
    default: assert(0 && "Unreachable");
    }
}

static VkBlendFactor toVkBlendFactor(BlendFactor type) {
    switch (type) {
    case BlendFactor::Zero: return VK_BLEND_FACTOR_ZERO;
    case BlendFactor::One: return VK_BLEND_FACTOR_ONE;
    case BlendFactor::SrcColor: return VK_BLEND_FACTOR_SRC_COLOR;
    case BlendFactor::DstColor: return VK_BLEND_FACTOR_DST_COLOR;
    case BlendFactor::SrcAlpha: return VK_BLEND_FACTOR_SRC_ALPHA;
    default: assert(0 && "Unreachable");
    }
}

static VkBlendOp toVkBlendOp(BlendOp type) {
    switch (type) {
    case BlendOp::Add: return VK_BLEND_OP_ADD;
    case BlendOp::Subtract: return VK_BLEND_OP_SUBTRACT;
    case BlendOp::Rev_Subtract: return VK_BLEND_OP_REVERSE_SUBTRACT;
    case BlendOp::Min: return VK_BLEND_OP_MIN;
    case BlendOp::Max: return VK_BLEND_OP_MAX;
    default: assert(0 && "Unreachable");
    }
}

static VkPipelineStageFlags2 toVkStage(Stage stage) {
    switch(stage) {
    case Stage::Transfer: return VK_PIPELINE_STAGE_2_TRANSFER_BIT;
    case Stage::Compute: return VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT;
    case Stage::RasterColorOut: return VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
    case Stage::FragmentShader: return VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
    case Stage::VertexShader: return VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT;
    case Stage::BuildBVH: return VK_PIPELINE_STAGE_2_ACCELERATION_STRUCTURE_BUILD_BIT_KHR;
    case Stage::All: return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    default: return VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
    }
}
