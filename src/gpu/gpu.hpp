#ifndef GPU_HPP
#define GPU_HPP
#include <vulkan/vulkan.h>
#include <GLFW/glfw3.h>
#include "ctl/allocator.hpp"
#include "ctl/types.hpp"
#include "ctl/string.hpp"
#include "ctl/array.hpp"
#include "ctl/slab.hpp"

using namespace ctl;

enum class Memory {
    Default = 0,
    GPU,
    Readback
};

enum class TextureType {
    D1,
    D2,
    D3,
};

enum class TextureFormat {
    Default = 0,
    RGBA8_Unorm,
    BGRA8_Unorm,
    RGBA8_SRGB,
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

enum class DepthFlags {
    None  = 0,
    Read  = 1 << 0,
    Write = 1 << 1,
};

inline DepthFlags operator|(DepthFlags a, DepthFlags b) { return static_cast<DepthFlags>(static_cast<Uint32>(a) | static_cast<Uint32>(b)); }
inline Bool operator&(DepthFlags a, DepthFlags b) { return static_cast<Uint32>(a) & static_cast<Uint32>(b); }

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

enum class PrimitiveTopology {
    TriangleList  = 0,
    TriangleStrip = 1,
    LineList      = 2,
    LineStrip     = 3,
    PointList     = 4,
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

enum class AllocationType {
    Default,
    Descriptor,
};

enum class Queue {
    Main = 0,
    Compute,
    Transfer,
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

enum class LoadOp {
    Clear,
    Load,
    DontCare
};

enum class StoreOp {
    Store,
    DontCare
};

namespace gpu {

    template<typename Tag>
    struct Handle {
        Uint32 index = ~0_u32;
        Uint32 generation = 0;

        constexpr Bool is_valid() { return index != ~0_u32; }
        constexpr Bool operator==(const Handle& other) {
            return index == other.index && generation == other.generation;
        }
    };

    struct TextureTag {};
    struct ShaderTag {};
    struct PipelineTag {};
    struct AllocTag {};
    struct CommandBufferTag {};
    struct SemaphoreTag {};

    using TextureHandle = Handle<TextureTag>;
    using ShaderHandle = Handle<ShaderTag>;
    using PipelineHandle = Handle<PipelineTag>;
    using AllocHandle = Handle<AllocTag>;
    using CommandBuffer = Handle<CommandBufferTag>;
    using SemaphoreHandle = Handle<SemaphoreTag>;

    template<typename T, typename Tag>
    struct ResourcePool {
    private:
        struct Slot {
            T data;
            Uint32 generation;
        };
        Slab slab_;

    public:
        constexpr ResourcePool(Allocator& allocator, Ulen chunkCapacity = 256)
            : slab_{allocator, sizeof(Slot), chunkCapacity}
        {}

        template<typename U>
        Maybe<Handle<Tag>> add(U&& item) {
            if (auto ref = slab_.allocate()) {
                Slot *slot = reinterpret_cast<Slot*>(slab_[*ref]);

                new (&slot->data, Nat{}) T{forward<U>(item)};

                if (slot->generation == 0) slot->generation = 1;

                return Handle<Tag>{
                    ref->index,
                    slot->generation
                };
            }
            return {};
        }

        T* get(Handle<Tag> handle) {
            if (!handle.is_valid()) return nullptr;

            SlabRef ref {handle.index};
            Slot *slot = reinterpret_cast<Slot*>(slab_[ref]);

            if (slot->generation != handle.generation) {
                return nullptr;
            }

            return &slot->data;
        }

        void remove(Handle<Tag> handle) {
            if (!handle.is_valid()) return;

            SlabRef ref{ handle.index };
            Slot *slot = reinterpret_cast<Slot*>(slab_[ref]);

            if (slot->generation == handle.generation) {
                if constexpr(!TriviallyDestructible<T>) {
                    slot->data.~T();
                }

                slot->generation += 1;

                slab_.deallocate(ref);
            }
        }
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

    struct DepthState {
        DepthFlags mode;
        CompareOp compare;
    };

    struct PipelineDesc {
        ShaderHandle vs;
        ShaderHandle fs;

        Slice<TextureFormat> colorFormats;
        TextureFormat depthFormat = TextureFormat::Default;
        TextureFormat stencilFormat = TextureFormat::Default;
    };
    //struct PipelineDesc {
    //    ShaderHandle vs;
    //    ShaderHandle fs;

    //    Slice<TextureFormat> colorFormats;
    //    TextureFormat depthFormat;
    //    TextureFormat stencilFormat;

    //    BlendState blendState;
    //    DepthState depthState;

    //    PrimitiveTopology topology;

    //    CullMode cullMode;
    //    FrontFace frontFace;
    //};

    struct SwapchainFrameData {
        VkCommandPool commandPool;
        VkCommandBuffer transitionCmd;
    };

    struct Swapchain {
        constexpr Swapchain(SystemAllocator& allocator)
            : images{allocator}
            , imageViews{allocator}
            , presentSemaphores{allocator}
            , textureHandles{allocator}
        {}

        VkSwapchainKHR handle = VK_NULL_HANDLE;
        Uint32 width;
        Uint32 height;
        Uint32 currentImageIndex;
        VkFormat imageFormat = VK_FORMAT_UNDEFINED;
        Array<VkImage> images;
        Array<VkImageView> imageViews;
        Array<VkSemaphore> presentSemaphores;
        Array<TextureHandle> textureHandles;
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
        ctl::Slice<T> cpu;
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
        constexpr Arena(Allocator& allocator)
            : blocks_{allocator}
        {}

        void init(Uint64 blockSize = 16 * 1024 * 1024, Memory memType = Memory::Default);

        void destroy();

        void reset();

        RawPtr allocRaw(Uint64 size, Uint64 count, Uint64 alignment = 16);

        template<typename T>
        Ptr<T> alloc(void) {
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

    struct Texture {
        Uint32 width;
        Uint32 height;
        VkFormat format;
        VkImage image;
        VkImageView view;
    };

    struct RenderAttachment {
        Texture texture;
        LoadOp loadOp = LoadOp::Clear;
        StoreOp storeOp = StoreOp::Store;
        Float32 clearColor[4] = { 0.0f, 0.0f, 0.0f, 0.0f };
    };

    struct RenderPassDesc {
        ctl::Slice<RenderAttachment> colorAttachments;
        Uint32 renderArea[2];
    };

    [[nodiscard]] Bool init(TemporaryAllocator& temp_alloc, GLFWwindow *window);

    void shutdown();

    Swapchain createSwapchain(Uint32 width, Uint32 height, Uint32 framesInFlight);

    void destroySwapchain(Swapchain& swapchain);

    void recreateSwapchain(Uint32 width, Uint32 height);

    void swapchainInit(VkSurfaceKHR surfafce, Uint32 width, Uint32 height, Uint32);

    void swapchainResize(TemporaryAllocator& temp_alloc, Uint32 width, Uint32 height);

    void swapchainInitFromGlfw(TemporaryAllocator& temp_alloc, GLFWwindow *window, Uint32 frameInFlight);

    [[nodiscard]] ShaderHandle shaderCreate(TemporaryAllocator& temp_alloc, StringView filePath);

    void shaderDestroy(ShaderHandle shader);

    PipelineHandle graphicsPipelineCreate(TemporaryAllocator& temp_alloc, PipelineDesc desc);

    void graphicsPipelineDestroy(PipelineHandle pipeline);

    RawPtr memAllocRaw(Uint64 elSize, Uint64 elCount, Uint64 align, Memory memType = Memory::Default, AllocationType allocType = AllocationType::Default);

    void memFreeRaw(RawPtr ptr);

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
            .cpu = Slice<T>(static_cast<T*>(raw.cpu), count),
            .gpu = raw.gpu,
            .handle = raw.handle,
        };
    }

    template<typename T>
    void memFree(Ptr<T> addr) {
        if (!addr.is_valid()) return;
        RawPtr raw {addr.cpu, addr.gpu, addr.handle};
        memFreeRaw(raw);
    }

    template<typename T>
    void memFree(SlicePtr<T> addr) {
        if (!addr.is_valid()) return;
        RawPtr raw {addr.cpu.data(), addr.gpu, addr.handle};
        memFreeRaw(raw);
    }

    CommandBuffer commandsBegin(Queue queue = Queue::Main);

    void cmdMemCpyRaw(CommandBuffer cmd, RawPtr dst, RawPtr src, Uint64 bytes);

    template<typename T>
    void cmdMemCpy(CommandBuffer cmd, RawPtr dst, RawPtr src, Uint64 elementCount) {
        cmdMemCpyRaw(cmd, dst, src, elementCount * sizeof(T));
    }

    void cmdBarrier(CommandBuffer cmd, Stage before, Stage after, Hazard hazards = Hazard::None);

    void queueSubmit(Queue queue, CommandBuffer cmd);

    SemaphoreHandle semaphoreCreate(Uint64 initValue = 0);

    void semaphoreDestroy(SemaphoreHandle sem);

    void waitIdle();

    void semaphoreWait(SemaphoreHandle sem, Uint64 value);

    void cmdBeginRenderPass(CommandBuffer cmd, const RenderPassDesc& desc);
    void cmdEndRenderPass(CommandBuffer cmd);
    void cmdBindGraphicsPipeline(CommandBuffer cmd, PipelineHandle pipeline);

    void cmdDrawIndexedInstanced(CommandBuffer cmd, RawPtr vertexData, RawPtr fragmentData, RawPtr indices, Uint32 indexCount, Uint32 instanceCount = 1);

    void cmdAddWaitSemaphore(CommandBuffer cmd, SemaphoreHandle sem, Uint64 value);
    void cmdAddSignalSemaphore(CommandBuffer cmd, SemaphoreHandle sem, Uint64 value);

    Texture swapchainAcquireNext();

    void swapchainPresent(Queue queue, SemaphoreHandle waitSem, Uint64 waitValue);

    void cmdSetBlendState(CommandBuffer cmd, const BlendState& state);
    void cmdSetDepthState(CommandBuffer cmd, const DepthState& state);
    void cmdSetRasterizerState(CommandBuffer cmd, CullMode cull, FrontFace face, PrimitiveTopology topology = PrimitiveTopology::TriangleList);
}

#endif // GPU_HPP
