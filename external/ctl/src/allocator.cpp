#include "ctl/allocator.hpp"
#include "ctl/system.hpp"

#if defined(CTL_CFG_USE_MALLOC)
#include <string.h>
#endif

#if defined(CTL_ARCH_X64)
    #include <emmintrin.h>
    #pragma message("INFO: Using SIMD x86_64")
#elif defined(CTL_ARCH_ARM64)
    #include <arm_neon.h>
    #pragma message("INFO: Using SIMD ARM64")
#elif defined(CTL_ARCH_WASM) && defined(__wasm_simd128__)
    #include <wasm_simd128.h>
    #pragma message("INFO: Using SIMD WebAssembly v128")
#endif

#if CTL_HAS_FEATURE(address_sanitizer) && defined(__SANITIZE_ADDRESS__)
    extern "C" void __asan_poison_memory_region(void const volatile*, decltype(sizeof 0));
    extern "C" void __asan_unpoison_memory_region(void const volatile*, decltype(sizeof 0));
    #define ASAN_POISON_MEMORY_REGION(addr, size) \
    	__asan_poison_memory_region(reinterpret_cast<volatile const void *>(addr), (size))
    #define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
    	__asan_unpoison_memory_region(reinterpret_cast<volatile const void *>(addr), (size))
#else
    #define ASAN_POISON_MEMORY_REGION(addr, size) \
    	(static_cast<void>(addr), static_cast<void>(size))
    #define ASAN_UNPOISON_MEMORY_REGION(addr, size) \
    	(static_cast<void>(addr), static_cast<void>(size))
#endif

#if CTL_HAS_INCLUDE(<valgrind/valgrind.h>) && CTL_HAS_INCLUDE(<valgrind/memcheck.h>)
    #include <valgrind/valgrind.h>
    #include <valgrind/memcheck.h>
#else
    #define VALGRIND_MALLOCLIKE_BLOCK(...)
    #define VALGRIND_FREELIKE_BLOCK(...)
    #define VALGRIND_RESIZEINPLACE_BLOCK(...)
    #define VALGRIND_MAKE_MEM_NOACCESS(...)
    #define VALGRIND_MAKE_MEM_DEFINED(...)
    #define VALGRIND_MAKE_MEM_UNDEFINED(...)
#endif

namespace ctl {

#define ASSERT(...)

    void Allocator::memzero(Address addr, Ulen len) {
        auto dst = reinterpret_cast<Uint8*>(addr);

        if (len < 16) {
            if (len == 0) return;
            if (len >= 8) {
                *reinterpret_cast<Uint64*>(dst)           = 0_u64;
                *reinterpret_cast<Uint64*>(dst + len - 8) = 0_u64;
            } else if (len >= 4) {
                *reinterpret_cast<Uint32*>(dst)           = 0_u32;
                *reinterpret_cast<Uint32*>(dst + len - 4) = 0_u32;
            } else if (len >= 2) {
                *reinterpret_cast<Uint16*>(dst)           = 0_u16;
                *reinterpret_cast<Uint16*>(dst + len - 2) = 0_u16;
            } else {
                dst[0] = 0_u8;
            }
            return;
        }

#if defined(CTL_ARCH_X64)
        const __m128i zero = _mm_setzero_si128();
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i),      zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16), zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32), zero);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48), zero);
        }

        // Middle remainder: 16 bytes per iteration
        for (; i + 16 <= len; i += 16) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), zero);
        }

        // Tail: overlapping 16-byte store handle 1..15 remaining bytes.
        if (i < len) {
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + len - 16), zero);
        }
#elif defined(CTL_ARCH_ARM64)
        const uint8x16_t zero = vdupq_n_u8(0);
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            vst1q_u8(dst + i,      zero);
            vst1q_u8(dst + i + 16, zero);
            vst1q_u8(dst + i + 32, zero);
            vst1q_u8(dst + i + 48, zero);
        }
        for (; i + 16 <= len; i += 16) {
            vst1q_u8(dst + i, zero);
        }

        // Tail: overlapping 16-byte store handle 1..15 remaining bytes.
        if (i < len) {
            vst1q_u8(dst + len - 16, zero);
        }
#elif defined(CTL_ARCH_WASM)
    #if defined(__wasm_bulk_memory__)
        __builtin_memset(dst, 0, len);
    #elif defined(__wasm_simd128__)
        const v128_t zero = wasm_i8x16_splat(0);
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            wasm_v128_store(dst + i,      zero);
            wasm_v128_store(dst + i + 16, zero);
            wasm_v128_store(dst + i + 32, zero);
            wasm_v128_store(dst + i + 48, zero);
        }
        for (; i + 16 <= len; i += 16) {
            wasm_v128_store(dst + i, zero);
        }
        if (i < len) {
            wasm_v128_store(dst + len - 16, zero);
        }
    #else
        // Scalar fallback for unknown architectures: word + byte loop
        const auto n_words = len / sizeof(Uint64);
        const auto n_bytes = len % sizeof(Uint64);
        const auto dst_w = reinterpret_cast<Uint64*>(dst);
        const auto dst_b = reinterpret_cast<Uint8*>(dst_w + n_words);
        for (Ulen i = 0; i < n_words; i++) dst_w[i] = 0_u64;
        for (Ulen i = 0; i < n_bytes; i++) dst_b[i] = 0_u8;
    #endif
#else
        // Scalar fallback for unknown architectures: word + byte loop
        const auto n_words = len / sizeof(Uint64);
        const auto n_bytes = len % sizeof(Uint64);
        const auto dst_w = reinterpret_cast<Uint64*>(dst);
        const auto dst_b = reinterpret_cast<Uint8*>(dst_w + n_words);
        for (Ulen i = 0; i < n_words; i++) dst_w[i] = 0_u64;
        for (Ulen i = 0; i < n_bytes; i++) dst_b[i] = 0_u8;
#endif
    }

    void Allocator::memcopy(Address dst_addr, Address src_addr, Ulen len) {
        auto dst = reinterpret_cast<Uint8*>(dst_addr);
        auto src = reinterpret_cast<Uint8*>(src_addr);

        if (len < 16) {
            if (len == 0) return;
            if (len >= 8) {
                const Uint64 a = *reinterpret_cast<const Uint64*>(src);
                const Uint64 b = *reinterpret_cast<const Uint64*>(src + len - 8);
                *reinterpret_cast<Uint64*>(dst)           = a;
                *reinterpret_cast<Uint64*>(dst + len - 8) = b;
            } else if (len >= 4) {
                const Uint32 a = *reinterpret_cast<const Uint32*>(src);
                const Uint32 b = *reinterpret_cast<const Uint32*>(src + len - 4);
                *reinterpret_cast<Uint32*>(dst)           = a;
                *reinterpret_cast<Uint32*>(dst + len - 4) = b;
            } else if (len >= 2) {
                const Uint16 a = *reinterpret_cast<const Uint16*>(src);
                const Uint16 b = *reinterpret_cast<const Uint16*>(src + len - 2);
                *reinterpret_cast<Uint16*>(dst)           = a;
                *reinterpret_cast<Uint16*>(dst + len - 2) = b;
            } else {
                dst[0] = src[0];
            }
            return;
        }

#if defined(CTL_ARCH_X64)
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            const __m128i a = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            const __m128i b = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 16));
            const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 32));
            const __m128i d = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i + 48));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i),      a);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 16), b);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 32), c);
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i + 48), d);
        }

        for (; i + 16 <= len; i += 16) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + i));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + i), v);
        }

        if (i < len) {
            const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i*>(src + len - 16));
            _mm_storeu_si128(reinterpret_cast<__m128i*>(dst + len - 16), v);
        }
#elif defined(CTL_ARCH_ARM64)
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            const uint8x16_t a = vld1q_u8(src + i);
            const uint8x16_t b = vld1q_u8(src + i + 16);
            const uint8x16_t c = vld1q_u8(src + i + 32);
            const uint8x16_t d = vld1q_u8(src + i + 48);
            vst1q_u8(dst + i,      a);
            vst1q_u8(dst + i + 16, b);
            vst1q_u8(dst + i + 32, c);
            vst1q_u8(dst + i + 48, d);
        }

        for (; i + 16 <= len; i += 16) {
            vst1q_u8(dst + i, vld1q_u8(src + i));
        }

        if (i < len) {
            const uint8x16_t v = vld1q_u8(src + len - 16);
            vst1q_u8(dst + len - 16, v);
        }
#elif defined(CTL_ARCH_WASM)
    #if defined(__wasm_bulk_memory__)
        __builtin_memcpy(dst, src, len);
    #elif defined(__wasm_simd128__)
        Ulen i = 0;
        for (; i + 64 <= len; i += 64) {
            const v128_t a = wasm_v128_load(src + i);
            const v128_t b = wasm_v128_load(src + i + 16);
            const v128_t c = wasm_v128_load(src + i + 32);
            const v128_t d = wasm_v128_load(src + i + 48);
            wasm_v128_store(dst + i,      a);
            wasm_v128_store(dst + i + 16, b);
            wasm_v128_store(dst + i + 32, c);
            wasm_v128_store(dst + i + 48, d);
        }
        for (; i + 16 <= len; i += 16) {
            wasm_v128_store(dst + i, wasm_v128_load(src + i));
        }

        if (i < len) {
            const v128_t v = wasm_v128_load(src + len - 16);
            wasm_v128_store(dst + len - 16, v);
        }
    #else
        for (Ulen i = 0; i < len; i++) {
            dst[i] = src[i];
        }
    #endif
#else
        for (Ulen i = 0; i < len; i++) {
            dst[i] = src[i];
        }
#endif
    }

    ArenaAllocator::ArenaAllocator(Address base, Ulen length)
        : region_{base, base + length}
        , cursor_{base}
    {
        //ASAN_POISON_MEMORY_REGION(base, length);
        //VALGRIND_MAKE_MEM_NOACCESS(base, length);
    }

    ArenaAllocator::~ArenaAllocator() {
        //VALGRIND_MAKE_MEM_UNDEFINED(region_.beg, region_.end - region_.beg);
    }

    Bool ArenaAllocator::owns(Address addr, Ulen len) const {
        return addr >= region_.beg && (addr + len <= region_.end);
    }

    void ArenaAllocator::reset() {
        cursor_ = region_.beg;
    }

    Address ArenaAllocator::alloc(Ulen req_len, Bool zero) {
        const Ulen new_len = round(req_len);
        if (cursor_ + new_len > region_.end) {
            return 0;
        }
        auto addr = cursor_;
        //ASAN_UNPOISON_MEMORY_REGION(addr, req_len);
        //VALGRIND_MALLOCLIKE_BLOCK(addr, req_len, 0, zero);
        cursor_ += new_len;
        if (zero) {
            memzero(addr, req_len);
        }
        return addr;
    }

    void ArenaAllocator::free(Address addr, Ulen req_old_len) {
        if (addr == 0) return;
        const Ulen old_len = round(req_old_len);
        ASSERT(addr >= region_.beg);
        //ASAN_POISON_MEMORY_REGION(addr, req_old_len);
        //VALGRIND_FREELIKE_BLOCK(addr, 0);
        if (addr + old_len == cursor_) {
            cursor_ -= old_len;
        }
    }

    void ArenaAllocator::shrink(Address addr, Ulen req_old_len, Ulen req_new_len) {
        const Ulen old_len = round(req_old_len);
        const Ulen new_len = round(req_new_len);
        ASSERT(addr >= region_.beg);
        //ASAN_POISON_MEMORY_REGION(addr + req_new_len, req_old_len - req_new_len);
        //VALGRIND_RESIZEINPLACE_BLOCK(addr, req_old_len, req_new_len, 0);
        if (addr + old_len == cursor_) {
            cursor_ -= old_len;
            cursor_ += new_len;
        }
    }

    Address ArenaAllocator::grow(Address src_addr, Ulen req_old_len, Ulen req_new_len, Bool zero) {
        const Ulen old_len = round(req_old_len);
        const Ulen new_len = round(req_new_len);
        ASSERT(src_addr >= region_.beg);
        const auto req_delta = req_new_len - req_old_len;
        if (src_addr + old_len == cursor_) {
            const auto delta = new_len - old_len;
            if (cursor_ + delta >= region_.end) {
                // Out of memory.
                return 0;
            }
            //ASAN_UNPOISON_MEMORY_REGION(src_addr + req_old_len, req_delta);
            //VALGRIND_RESIZEINPLACE_BLOCK(src_addr, req_old_len, req_new_len, 0);
            if (zero) {
                // RESIZEINPLACE doesn't appear to have a mechanism to specify the growed
                // area is initialized, so do it manually here.
                VALGRIND_MAKE_MEM_DEFINED(src_addr + req_old_len, req_delta);
                memzero(src_addr + req_old_len, req_delta);
            }
            cursor_ += delta;
            return src_addr;
        }
        // Otherwise allocate new memory and copy.
        const auto dst_addr = alloc(req_new_len, false);
        if (!dst_addr) {
            // Out of memory.
            return 0;
        }
        memcopy(dst_addr, src_addr, req_old_len);
        if (zero) {
            memzero(dst_addr + req_old_len, req_delta);
        }
        free(src_addr, req_old_len);
        return dst_addr;
    }

    TemporaryAllocator::~TemporaryAllocator() {
        for (auto node = head_; node; /**/) {
            const auto addr = reinterpret_cast<Address>(node);
            const auto next = node->next_;
            allocator_.free(addr, sizeof(Block) + node->arena_.length());
            node = next;
        }
    }

    Bool TemporaryAllocator::add(Ulen len) {
        // 2 MiB chunks and double in size until large enough for 'len'
        Ulen block_size = 2 << 20;
        while (block_size < len) {
            block_size *= 2;
        }
        const auto addr = allocator_.alloc(sizeof(Block) + block_size, false);
        if (!addr) {
            return false;
        }
        const auto ptr = reinterpret_cast<void*>(addr);
        const auto node = new (ptr, Nat{}) Block{block_size};
        if (tail_) {
            tail_->next_ = node;
            node->prev_ = tail_;
            tail_ = node;
        } else {
            tail_ = node;
            head_ = node;
        }
        return true;
    }

    void TemporaryAllocator::reset() {
        Block* curr = head_;
        while (curr) {
            curr->arena_.reset();
            curr = curr->next_;
        }
        tail_ = head_;
    }

    Address TemporaryAllocator::alloc(Ulen new_len, Bool zero) {
        new_len = round(new_len);
        if (!tail_ && !add(new_len)) {
            return 0;
        }
        if (const auto addr = tail_->arena_.alloc(new_len, zero)) {
            return addr;
        }
        if (tail_->next_) {
            tail_ = tail_->next_;
            return alloc(new_len, zero);
        }
        if (!add(new_len)) {
            return 0;
        }
        return alloc(new_len, zero);
    }

    void TemporaryAllocator::free(Address addr, Ulen old_len) {
        if (addr == 0) return;
        for (auto node = tail_; node; node = node->prev_) {
            if (node->arena_.owns(addr, old_len)) {
                node->arena_.free(addr, old_len);
                return;
            }
        }
    }

    void TemporaryAllocator::shrink(Address addr, Ulen old_len, Ulen new_len) {
        for (auto node = head_; node; node = node->next_) {
            if (node->arena_.owns(addr, old_len)) {
                node->arena_.shrink(addr, old_len, new_len);
                return;
            }
        }
    }

    Address TemporaryAllocator::grow(Address old_addr, Ulen old_len, Ulen new_len, Bool zero) {
        // Attempt in-place growth.
        for (auto node = head_; node; node = node->next_) {
            if (!node->arena_.owns(old_addr, old_len)) {
                continue;
            }
            if (auto new_addr = node->arena_.grow(old_addr, old_len, new_len, zero)) {
                return new_addr;
            }
        }
        // Could not grow in-place, allocate fresh memory.
        const auto new_addr = alloc(new_len, false);
        if (!new_addr) {
            return 0;
        }
        // Copy the old part over.
        memcopy(new_addr, old_addr, old_len);
        if (zero) {
            // Zero the remainder part if requested.
            memzero(new_addr + old_len, new_len - old_len);
        }
        free(old_addr, old_len);
        return new_addr;
    }

    Address SystemAllocator::alloc(Ulen new_len, Bool zero) {
        if (const auto ptr = Heap::allocate(new_len, zero)) {
            ASAN_UNPOISON_MEMORY_REGION(ptr, new_len);
            VALGRIND_MALLOCLIKE_BLOCK(ptr, new_len, 0, zero);
            return reinterpret_cast<Address>(ptr);
        }
        return 0;
    }

    void SystemAllocator::free(Address addr, Ulen old_len) {
        if (addr == 0) return;
        const auto ptr = reinterpret_cast<void *>(addr);
        Heap::deallocate(ptr, old_len);
        ASAN_POISON_MEMORY_REGION(ptr, old_len);
        VALGRIND_FREELIKE_BLOCK(ptr, 0);
    }

    void SystemAllocator::shrink(Address, Ulen, Ulen) {
        // no-op
    }

    Address SystemAllocator::grow(Address old_addr, Ulen old_len, Ulen new_len, Bool zero) {
        const auto new_ptr = Heap::allocate(new_len, false);
        if (!new_ptr) {
            return 0;
        }
        const auto new_addr = reinterpret_cast<Address>(new_ptr);
        memcopy(new_addr, old_addr, old_len);
        if (zero) {
            memzero(new_addr + old_len, new_len - old_len);
        }
        const auto old_ptr = reinterpret_cast<void *>(old_addr);
        Heap::deallocate(old_ptr, old_len);
        return new_addr;
    }

} // namespace ctl
