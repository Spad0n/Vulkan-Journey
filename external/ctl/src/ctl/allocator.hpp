#ifndef CTL_ALLOCATOR_HPP
#define CTL_ALLOCATOR_HPP
#include "types.hpp"
#include "forward.hpp"
#include "exchange.hpp"

/// @namespace ctl
/// @brief CTL is a lightweight C++ library providing essential STL-like features without relying on the standard library.
namespace ctl {

    /// @brief Abstract base class for memory allocators.
    ///
    /// Provides a polymorphic interface for memory management (`alloc`, `free`, `grow`, `shrink`)
    /// as well as typed helper templates for constructing/destroying objects.
    struct Allocator {
        /// @brief Fills a memory region with zeros.
	static void memzero(Address addr, Ulen len);

        /// @brief Copies memory from source to destination.
	static void memcopy(Address dst, Address src, Ulen len);

        /// @brief Rounds up a length to the nearest multiple of 16 bytes.
	static constexpr Ulen round(Ulen len) {
            return ((len + 16 - 1) / 16) * 16;
	}

        /// @brief Allocates a raw block of memory.
        /// @param length The size in bytes to allocate.
        /// @param zero If true, the memory is initialized to zero.
        /// @return The address of the allocated block, or 0 if allocation failed.
	virtual Address alloc(Ulen length, Bool zero) = 0;

        /// @brief Frees a raw block of memory.
        /// @param addr The address of the block to free.
        /// @param old_len The size of the block being freed (must match the allocation size).
	virtual void free(Address addr, Ulen old_len) = 0;

        /// @brief Shrinks an existing allocation in-place.
        /// @param addr The address of the block.
        /// @param old_len The current size of the block.
        /// @param new_len The desired new size (must be <= old_len).
	virtual void shrink(Address addr, Ulen old_len, Ulen new_len) = 0;

        /// @brief Grows an existing allocation, potentially moving it.
        /// @param addr The address of the block to grow.
        /// @param old_len The current size of the block.
        /// @param new_len The desired new size (must be >= old_len).
        /// @param zero If true, the newly added memory range is zero-initialized.
        /// @return The new address of the block (may be different from `addr`), or 0 on failure.
	virtual Address grow(Address addr, Ulen old_len, Ulen new_len, Bool zero) = 0;

	// --- Helper functions when working with typed data ---

        /// @brief Allocates memory for an array of T.
        /// @tparam T The type of elements.
        /// @param count The number of elements.
        /// @param zero If true, memory is zeroed.
        /// @return A pointer to the allocated memory, or nullptr on failure.
	template<typename T>
	T* allocate(Ulen count, Bool zero) {
            if (auto addr = alloc(count * sizeof(T), zero)) {
                return reinterpret_cast<T*>(addr);
            }
            return nullptr;
	}

        /// @brief Deallocates memory for an array of T.
        /// @note Does NOT call destructors. Use `destroy` for that.
	template<typename T>
	void deallocate(T* ptr, Ulen count) {
            auto addr = reinterpret_cast<Address>(ptr);
            free(addr, count * sizeof(T));
	}

        // --- Helpers for allocating+construct and destruct+deallocate objects ---

        /// @brief Allocates and constructs a single object of type T.
        /// @tparam T The type of object to create.
        /// @param args Arguments forwarded to the constructor.
        /// @return A pointer to the constructed object, or nullptr.
	template<typename T, typename... Ts>
	T* create(Ts&&... args) {
            if (auto data = allocate<T>(1, false)) {
                return new (data, Nat{}) T{forward<Ts>(args)...};
            }
            return nullptr;
	}

        /// @brief Destroys an object and deallocates its memory.
        ///Calls the destructor `~T()` then frees the memory.
	template<typename T>
	void destroy(T* obj) {
            if (obj) {
                obj->~T();
                deallocate(obj, 1);
            }
	}
    };

    /// @brief A linear allocator that allocates sequentially from a fixed memory region.
    ///
    /// Very fast. Deallocation is generally a no-op unless the freed block is the
    /// last one allocated (LIFO). `reset()` can be used to clear everything at once.
    struct ArenaAllocator : Allocator {
        /// @brief Constructs an arena from a raw memory range.
	ArenaAllocator(Address base, Ulen length);

	ArenaAllocator(const ArenaAllocator&) = delete;
	ArenaAllocator(ArenaAllocator&& other) = delete;
	~ArenaAllocator();

        /// @brief Checks if a memory range belongs to this arena.
	Bool owns(Address addr, Ulen len) const;

        /// @brief Resets the allocation cursor to the beginning (frees everything).
        void reset();

	virtual Address alloc(Ulen new_len, Bool zero);
	virtual void free(Address addr, Ulen old_len);
	virtual void shrink(Address addr, Ulen old_len, Ulen new_len);
	virtual Address grow(Address addr, Ulen old_len, Ulen new_len, Bool zero);

        /// @brief Returns the total capacity of the arena.
	constexpr Ulen length() const {
            return region_.end - region_.beg;
	}
    private:
	struct { Address beg, end; } region_;
	Address                      cursor_;
    };

    /// @brief An arena allocator that uses a stack-allocated buffer.
    /// @tparam E The size of the internal buffer in bytes.
    template<Ulen E>
    struct InlineAllocator : ArenaAllocator {
	constexpr InlineAllocator()
            : ArenaAllocator{reinterpret_cast<Address>(data_), E}
	{}
	InlineAllocator(const InlineAllocator&) = delete;
	InlineAllocator(InlineAllocator&&) = delete;
    private:
	alignas(16) Uint8 data_[E];
    };

    /// @brief An allocator that manages a linked list of memory blocks.
    ///
    /// Good for frame-based or scope-based allocations where data grows dynamically
    /// but is freed all at once. Backed by another allocator (parent).
    struct TemporaryAllocator : Allocator {
	TemporaryAllocator(const TemporaryAllocator&) = delete;

        /// @brief Move constructor. Takes ownership of the blocks.
	TemporaryAllocator(TemporaryAllocator&& other)
            : allocator_{other.allocator_}
            , head_{exchange(other.head_, nullptr)}
            , tail_{exchange(other.tail_, nullptr)}
	{}

        /// @brief Constructs a temporary allocator using a parent allocator for backing memory.
	constexpr TemporaryAllocator(Allocator& allocator)
            : allocator_{allocator}
	{}

        /// @brief Destructor. Frees all allocated blocks back to the parent allocator.
	~TemporaryAllocator();

        /// @brief Frees all allocations but keeps the memory blocks for reuse.
        void reset();

	virtual Address alloc(Ulen new_len, Bool zero);
	virtual void free(Address addr, Ulen old_len);
	virtual void shrink(Address addr, Ulen old_len, Ulen new_len);
	virtual Address grow(Address addr, Ulen old_len, Ulen new_len, Bool zero);
    private:
	// Add a new block to the temporary allocator.
	Bool add(Ulen len);
	struct Block {
            Block(Ulen length)
                : arena_{reinterpret_cast<Address>(data_), length}
            {}
            ArenaAllocator    arena_;
            Block*            prev_ = nullptr;
            Block*            next_ = nullptr;
            alignas(16) Uint8 data_[];
	};
	Allocator& allocator_;
	Block*     head_ = nullptr;
	Block*     tail_ = nullptr;
    };

    /// @brief A hybrid allocator optimized for small, local allocations.
    ///
    /// Tries to allocate from an internal stack buffer (`InlineAllocator`) first.
    /// If the stack buffer is full, it falls back to a `TemporaryAllocator`.
    /// @tparam E The size of the stack buffer in bytes.
    template<Ulen E>
    struct ScratchAllocator : Allocator {
	constexpr ScratchAllocator(Allocator& allocator)
            : temporary_{allocator}
	{}
	ScratchAllocator(const ScratchAllocator&) = delete;
	ScratchAllocator(ScratchAllocator&&) = delete;
	virtual Address alloc(Ulen new_len, Bool zero) {
            if (auto addr = inline_.alloc(new_len, zero)) return addr;
            return temporary_.alloc(new_len, zero);
	}
	virtual void free(Address addr, Ulen old_len) {
            if (inline_.owns(addr, old_len)) {
                inline_.free(addr, old_len);
            } else {
                temporary_.free(addr, old_len);
            }
	}
	virtual void shrink(Address addr, Ulen old_len, Ulen new_len) {
            if (inline_.owns(addr, old_len)) {
                inline_.shrink(addr, old_len, new_len);
            } else {
                temporary_.shrink(addr, old_len, new_len);
            }
	}
	virtual Address grow(Address old_addr, Ulen old_len, Ulen new_len, Bool zero) {
            if (inline_.owns(old_addr, old_len)) {
                if (auto new_addr = inline_.grow(old_addr, old_len, new_len, zero)) {
                    return new_addr;
                } else if (auto new_addr = temporary_.alloc(new_len, false)) {
                    // Could not grow in-place inside the inline allocator. Going to have to
                    // move the result to the temporary allocator instead.
                    memcopy(new_addr, old_addr, old_len);
                    if (zero) {
                        memzero(new_addr + old_len, new_len - old_len);
                    }
                    inline_.free(old_addr, old_len);
                    return new_addr;
                } else {
                    return 0;
                }
            }
            return temporary_.grow(old_addr, old_len, new_len, zero);
	}
    private:
	InlineAllocator<E> inline_;
	TemporaryAllocator temporary_;
    };

    /// @brief Wraps the operating system's memory allocator (malloc/free/mmap/VirtualAlloc).
    struct SystemAllocator : Allocator {
	virtual Address alloc(Ulen new_len, Bool zero);
	virtual void free(Address addr, Ulen old_len);
	virtual void shrink(Address, Ulen, Ulen);
	virtual Address grow(Address addr, Ulen old_len, Ulen new_len, Bool zero);
    };

} // namespace ctl

#endif // CTL_ALLOCATOR_HPP
