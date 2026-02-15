#include "ctl/allocator.hpp"
#include "ctl/system.hpp"

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
	const auto n_words = len / sizeof(Uint64);
	const auto n_bytes = len % sizeof(Uint64);
	const auto dst_w = reinterpret_cast<Uint64*>(addr);
	const auto dst_b = reinterpret_cast<Uint8*>(dst_w + n_words);
	for (Ulen i = 0; i < n_words; i++) dst_w[i] = 0_u64;
	for (Ulen i = 0; i < n_bytes; i++) dst_b[i] = 0_u8;
    }

    void Allocator::memcopy(Address dst, Address src, Ulen len) {
	const auto dst_b = reinterpret_cast<Uint8*>(dst);
	const auto src_b = reinterpret_cast<const Uint8*>(src);
	for (Ulen i = 0; i < len; i++) {
            dst_b[i] = src_b[i];
	}
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
            }
            if (zero) {
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
	for (auto node = tail_; node; /**/) {
            const auto addr = reinterpret_cast<Address>(node);
            const auto prev = node->prev_;
            allocator_.free(addr, sizeof(Block) + node->arena_.length());
            node = prev;
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
	tail_ = node;
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
