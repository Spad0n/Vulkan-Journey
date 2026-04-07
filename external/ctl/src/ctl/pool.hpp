#ifndef CTL_POOL_HPP
#define CTL_POOL_HPP
// #include "util/types.h"
#include "maybe.hpp"
#include "allocator.hpp"

namespace ctl {

    /// @brief A handle to an object stored in a Pool.
    /// safer than a raw pointer as it is just an index.
    struct PoolRef {
	Uint32 index;
    };

    struct Allocator;

    // Pool allocator, used to allocate objects of a fixed size. The pool also has a
    // fixed capacity (# of objects). Does not communicate using pointers but rather
    // PoolRef (plain typed index). Allocate object with allocate(), deallocate with
    // deallocate(). The address (pointer) of the object can be looked-up by passing
    // the PoolRef to operator[] like a key.
    struct Stream;

    /// @brief A fixed-size, fixed-capacity object allocator (Object Pool).
    /// 
    /// Memory is contiguous. Objects are referenced by `PoolRef` indices.
    /// Useful for game entities, particles, or nodes to avoid fragmentation.
    struct Pool {
        /// @brief Creates a new Pool.
        /// @param allocator Backing allocator for the pool memory.
        /// @param size Size of a single object in bytes.
        /// @param capacity Maximum number of objects.
        /// @return A new Pool or empty on allocation failure.
	static Maybe<Pool> create(Allocator& allocator, Ulen size, Ulen capacity);

        /// @brief Loads a Pool from a binary stream.
	static Maybe<Pool> load(Allocator& allocator, Stream& stream);

        /// @brief Serializes the Pool state (and data) to a binary stream.
	Bool save(Stream& stream) const;

	Pool(Pool&& other);

        /// @brief Destroys elements and deallocates memory.
        void destroy();

        /// @brief Returns the number of active objects in the pool.
	[[nodiscard]] CTL_FORCEINLINE constexpr auto length() const { return length_; }
	[[nodiscard]] CTL_FORCEINLINE constexpr auto is_empty() const { return length_ == 0; }

	constexpr Pool(const Pool&) = delete;
	constexpr Pool& operator=(const Pool&) = delete;

	Pool& operator=(Pool&& other) {
            return *new (drop(), Nat{}) Pool{move(other)};
	}

        /// @brief Allocates a slot for a new object.
        /// @return A reference (index) to the allocated slot, or empty if full.
	Maybe<PoolRef> allocate();

        /// @brief Deallocates the slot at the given reference.
	void deallocate(PoolRef ref);

        /// @brief Accesses the memory at the given reference.
        /// @return Raw pointer to the object data (must be cast to T*).
	CTL_FORCEINLINE constexpr auto operator[](PoolRef ref) { return data_ + size_ * ref.index; }

        /// @brief Accesses the memory at the given reference (const).
	CTL_FORCEINLINE constexpr auto operator[](PoolRef ref) const { return data_ + size_ * ref.index; }

    private:
	using Word = Uint64;
	static constexpr const auto BITS = Uint32(sizeof(Word) * 8);
	constexpr Pool(Allocator& allocator, Ulen size, Ulen length, Ulen capacity, Uint8* data, Word* used)
            : allocator_{allocator}
            , size_{size}
            , length_{length}
            , capacity_{capacity}
            , data_{data}
            , used_{used}
            , last_{0}
	{}

	Pool* drop() {
            allocator_.deallocate(data_, size_ * capacity_);
            allocator_.deallocate(used_, capacity_ / BITS);
            return this;
	}

	Allocator& allocator_;
	Ulen       size_;      // Size of an object in the pool
	Ulen       length_;    // # of objects in the pool
	Ulen       capacity_;  // Always a multiple of 32 (max # of objects in pool)
	Uint8*     data_;      // Object memory
	Word*      used_;      // Bitset where bit N indicates object N is in-use or not.
	Uint32     last_;      // Last w_index
    };

}

#endif // CTL_POOL_HPP
