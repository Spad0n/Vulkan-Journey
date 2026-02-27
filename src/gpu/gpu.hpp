#ifndef GPU_HPP
#define GPU_HPP

#include <vulkan/vulkan.h>
#include "vk_mem_alloc.h"
#include "ctl/slab.hpp"
#include "ctl/array.hpp"
#include "ctl/string.hpp"

using namespace ctl;

enum class TextureFormat {
    Default = 0,
    RGBA8_Unorm,
    BGRA8_Unorm,
    RGBA8_SRGB,
    BGRA8_SRGB,
    D32_Float,
    RGBA16_Float,
    RGBA32_Float,
    BC1_RGBA_Unorm,
    BC3_RGBA_Unorm,
    BC7_RGBA_Unorm,
    ASTC_4x4_RGBA_Unorm,
    ETC2_RGB8_Unorm,
    ETC2_RGBA8_Unorm,
    EAC_R11_Unorm,
    EAC_RG11_Unorm,
};

enum class AllocationType {
    Default,
    Descriptor,
};

enum class Memory {
    Default = 0,
    GPU,
    Readback
};

enum class LoadOp {
    Clear,
    Load,
    DontCare
};

enum class StoreOp {
    Store,
    DontCare
};

enum class BlendOp {
    Add,
    Subtract,
    Rev_Subtract,
    Min,
    Max
};

enum class BlendFactor {
    Zero,
    One,
    SrcColor,
    DstColor,
    SrcAlpha
};

enum class DepthFlags {
    None  = 0,
    Read  = 1 << 0,
    Write = 1 << 1,
};

inline DepthFlags operator|(DepthFlags a, DepthFlags b) { return static_cast<DepthFlags>(static_cast<Uint32>(a) | static_cast<Uint32>(b)); }
inline Bool operator&(DepthFlags a, DepthFlags b) { return static_cast<Uint32>(a) & static_cast<Uint32>(b); }

enum class CompareOp {
    Never = 0,
    Less,
    Equal,
    LessEqual,
    Greater,
    NotEqual,
    GreaterEqual,
    Always
};

enum class CullMode {
    None,
    Front,
    Back,
    FrontAndBack
};

enum class FrontFace{
    CounterClockwise,
    Clockwise
};

enum class PrimitiveTopology {
    TriangleList  = 0,
    TriangleStrip = 1,
    LineList      = 2,
    LineStrip     = 3,
    PointList     = 4,
};

enum class Stage {
    Transfer,
    Compute,
    RasterColorOut,
    FragmentShader,
    VertexShader,
    BuildBVH,
    All
};

enum class Hazard {
    None = 0,
    DrawArguments = 1 << 0,
    Descriptors = 1 << 1,
    DepthStencil = 1 << 2,
    BVHs = 1 << 3
};

inline Hazard operator|(Hazard a, Hazard b) { return static_cast<Hazard>(static_cast<Uint32>(a) | static_cast<Uint32>(b)); }
inline Bool operator&(Hazard a, Hazard b) { return static_cast<Uint32>(a) & static_cast<Uint32>(b); }

namespace gpu {
    struct Swapchain {
        constexpr Swapchain(Allocator& allocator)
            : images{allocator}
            , imageViews{allocator}
            , presentSemaphores{allocator}
        {}

        Array<VkImage> images;
        Array<VkImageView> imageViews;
        Array<VkSemaphore> presentSemaphores;
        VkSwapchainKHR handle;
        VkFormat imageFormat;
        Uint32 width;
        Uint32 height;
    };

    template<typename Tag>
    struct Handle {
        Uint32 index = ~0_u32;
        Uint32 generation = 0;

        [[nodiscard]] constexpr Bool is_valid() { return index != ~0_u32; }
        constexpr Bool operator==(const Handle& other) {
            return index == other.index && generation == other.generation;
        }
        constexpr Bool operator!=(const Handle& other) {
            return !(*this == other);
        }
    };

    struct AllocTag {};
    struct CommandBufferTag {};
    struct GraphicsPipelineTag {};
    struct ShaderTag {};

    using AllocHandle = Handle<AllocTag>;
    using CommandBufferHandle = Handle<CommandBufferTag>;
    using GraphicsPipelineHandle = Handle<GraphicsPipelineTag>;
    using ShaderHandle = Handle<ShaderTag>;

    template<typename T, typename Tag>
    struct ResourcePool {
        constexpr ResourcePool(Allocator& allocator, Ulen chunkCapacity = 256)
            : slab_{allocator, sizeof(T), chunkCapacity}
            , generations_{allocator}
        {}

        template<typename U>
        Maybe<Handle<Tag>> add(U&& item) {
            if (auto ref = slab_.allocate()) {
                if (ref->index >= generations_.length()) {
                    generations_.resize(ref->index + 1);
                }
                if (generations_[ref->index] == 0) {
                    generations_[ref->index] = 1;
                }

                T* dataPtr = reinterpret_cast<T*>(slab_[*ref]);
                new (dataPtr, Nat{}) T{forward<U>(item)};

                return Handle<Tag> {
                    ref->index,
                    generations_[ref->index],
                };
            }
            return {};
        }

        T* get(Handle<Tag> handle) {
            if (!handle.is_valid()) return nullptr;
            if (handle.index >= generations_.length()) return nullptr;
            if (generations_[handle.index] != handle.generation) return nullptr;

            SlabRef ref {handle.index};
            return reinterpret_cast<T*>(slab_[ref]);
        }

        const T* get(Handle<Tag> handle) const {
            if (!handle.is_valid()) return nullptr;
            if (handle.index >= generations_.length()) return nullptr;
            if (generations_[handle.index] != handle.generation) return nullptr;

            SlabRef ref {handle.index};
            return reinterpret_cast<const T*>(slab_[ref]);
        }

        void remove(Handle<Tag> handle) {
            if (!handle.is_valid()) return;
            if (handle.index >= generations_.length()) return;

            if (generations_[handle.index] == handle.generation) {
                SlabRef ref{handle.index};
                T* dataPtr = reinterpret_cast<T*>(slab_[ref]);

                if constexpr(!TriviallyDestructible<T>) {
                    dataPtr->~T();
                }

                generations_[handle.index] += 1;

                slab_.deallocate(ref);
            }
        }
    private:
        Slab slab_;
        Array<Uint32> generations_;
    };

    struct RawPtr {
        void* cpu = nullptr;
        Uint64 gpu = 0;
        AllocHandle handle;

        constexpr Bool is_valid() const { return cpu != nullptr && gpu != 0; }
        constexpr operator Bool() const { return is_valid(); }
    };

    template<typename T>
    struct Ptr {
        T* cpu = nullptr;
        Uint64 gpu = 0;
        AllocHandle handle;

        constexpr Bool is_valid() const { return cpu != nullptr && gpu != 0; }
        constexpr operator Bool() const { return is_valid(); }
        constexpr operator RawPtr() const { return { static_cast<void*>(cpu), gpu, handle }; }

        T* operator->() const { return cpu; }
        T& operator*() const { return *cpu; }
    };

    template<typename T>
    struct SlicePtr {
        Slice<T> cpu;
        Uint64 gpu = 0;
        AllocHandle handle;

        constexpr Bool is_valid() const { return !cpu.is_empty() && gpu != 0; }
        constexpr operator Bool() const { return is_valid(); }
        constexpr operator RawPtr() const { return { static_cast<void*>(const_cast<T*>(cpu.data())), gpu, handle }; }

        T& operator[](Ulen index) { return cpu[index]; }
        const T& operator[](Ulen index) const { return cpu[index]; }
        Ulen length() const { return cpu.length(); }
    };

    struct Arena {
        constexpr Arena(Allocator& allocator, Uint64 blockSize = 16 * 1024 * 1024)
            : blocks_{allocator}
            , blockSize_{blockSize}
        {}

        void destroy();

        void reset();

        RawPtr allocRaw(Uint64 size, Uint64 count, Uint64 alignment = 16);

        template<typename T>
        Ptr<T> alloc() {
            RawPtr raw = allocRaw(sizeof(T), 1, alignof(T));
            return { static_cast<T*>(raw.cpu), raw.gpu, raw.handle };
        }

        template<typename T>
        SlicePtr<T> alloc(Uint64 count) {
            RawPtr raw = allocRaw(sizeof(T), count, alignof(T));
            return { Slice<T>(static_cast<T*>(raw.cpu), count), raw.gpu, raw.handle };
        }
    private:
        struct Block {
            RawPtr ptr;
            Uint64 size;
        };

        Block allocateBlock(Uint64 requiredSize);

        Array<Block> blocks_;
        Uint64 blockSize_ = 0;
        Uint64 currentOffset_ = 0;
        Uint64 currentBlockIdx_ = 0;
        Memory memType_ = Memory::Default;
    };

    struct RenderAttachment {
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        Float32 clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct RenderPassDesc {
        Slice<RenderAttachment> colorAttachment;
        Uint32 renderArea[2];
    };

    RawPtr memAllocRaw(Uint64 elSize, Uint64 elCount, Uint64 align, Memory memType = Memory::Default, AllocationType allocType = AllocationType::Default);

    void memFreeRaw(RawPtr& ptr);

    template<typename T>
    Ptr<T> memAlloc(Memory memType = Memory::Default, AllocationType allocType = AllocationType::Default) {
        RawPtr raw = memAllocRaw(sizeof(T), 1, alignof(T), memType, allocType);
        return {
            .cpu = static_cast<T*>(raw.cpu),
            .gpu = raw.gpu,
            .handle = raw.handle,
        };
    }

    template<typename T>
    SlicePtr<T> memAlloc(Uint64 count, Memory memType = Memory::Default, AllocationType allocType = AllocationType::Default) {
        RawPtr raw = memAllocRaw(sizeof(T), count, alignof(T), memType, allocType);
        return {
            .cpu = Slice(static_cast<T*>(raw.cpu), count),
            .gpu = raw.gpu,
            .handle = raw.handle,
        };
    }

    template<typename T>
    void memFree(Ptr<T> addr) {
        if (!addr.is_valid()) return;
        memFreeRaw(addr);
    }

    template<typename T>
    void memFree(SlicePtr<T> addr) {
        if (!addr.is_valid()) return;
        RawPtr raw = addr;
        memFreeRaw(raw);
    }

    Bool init(TemporaryAllocator& temp_alloc, GLFWwindow *window);

    void shutdown(void);

    void waitIdle(void);

    Swapchain createSwapchain(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height, Uint32 framesInFlight);

    void destroySwapchain(Swapchain& swapchain);

    void swapchainInit(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height, Uint32 framesInFlight);

    ShaderHandle shaderCreate(TemporaryAllocator& temp_alloc, StringView filePath);

    void shaderDestroy(ShaderHandle& shader);

    struct PipelineDesc {
        ShaderHandle vs;
        ShaderHandle fs;

        Slice<TextureFormat> colorFormats;
        TextureFormat depthFormat = TextureFormat::Default;
        TextureFormat stencilFormat = TextureFormat::Default;
    };

    GraphicsPipelineHandle graphicsPipelineCreate(TemporaryAllocator& temp_alloc, PipelineDesc desc);

    void graphicsPipelineDestroy(GraphicsPipelineHandle& pipeline);

    struct DepthState {
        DepthFlags mode;
        CompareOp compare;
    };

    struct BlendState {
        Bool enable;
        BlendOp colorOp;
        BlendFactor srcColorFactor;
        BlendFactor dstColorFactor;
        BlendOp alphaOp;
        BlendFactor srcAlphaFactor;
        BlendFactor dstAlphaFactor;
        Uint8 colorWriteMask;
    };

    CommandBufferHandle commandsBegin(void);

    void cmdMemCpyRaw(CommandBufferHandle cmd, RawPtr dst, RawPtr src, Uint64 bytes);

    template<typename T>
    void cmdMemCpy(CommandBufferHandle cmd, RawPtr dst, RawPtr src, Uint64 elementCount) {
        cmdMemCpyRaw(cmd, dst, src, elementCount * sizeof(T));
    }

    Arena& getFrameArena();

    CommandBufferHandle beginFrame();

    void endFrame(CommandBufferHandle cmdHandle);

    void cmdBeginRendering(CommandBufferHandle cmd, LoadOp loadOp, StoreOp storeOp, Float32 r, Float32 g, Float32 b, Float32 a);

    void cmdEndRendering(CommandBufferHandle cmd);

    void cmdSetViewportScissor(CommandBufferHandle cmd, Uint32 width, Uint32 height);

    void cmdPushConstants(CommandBufferHandle cmd, const void* data, Uint32 size);

    CommandBufferHandle beginSingleTimeCommands();

    void endSingleTimeCommands(CommandBufferHandle cmd);

    void queueSubmit(CommandBufferHandle cmd);

    void cmdBindGraphicsPipeline(CommandBufferHandle cmd, GraphicsPipelineHandle pipeline);

    void cmdSetBlendState(CommandBufferHandle cmd, const BlendState& state);

    void cmdSetDepthState(CommandBufferHandle cmd, const DepthState& state);

    void cmdSetRasterizerState(CommandBufferHandle cmd, CullMode cull, FrontFace face, PrimitiveTopology topology);

    void cmdBarrier(CommandBufferHandle cmd, Stage before, Stage after, Hazard hazards = Hazard::None);

    void cmdDrawInstanced(CommandBufferHandle& cmd, Uint32 vertexCount, Uint32 instanceCount, Uint32 firstVertex, Uint32 firstInstance);
}

#endif // GPU_HPP
