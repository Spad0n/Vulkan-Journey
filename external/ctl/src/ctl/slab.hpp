#ifndef CTL_SLAB_HPP
#define CTL_SLAB_HPP
#include "pool.hpp"
#include "array.hpp"

namespace ctl {

    /// @brief A handle to an object stored in a Slab.
    struct SlabRef {
	Uint32 index;
    };

    /// @brief A growable pool allocator.
    /// 
    /// Composed of multiple fixed-size `Pool`s (caches). When one is full,
    /// a new one is allocated. Provides stable indices (`SlabRef`) across growths.
    struct Stream;
    struct Slab {
        /// @brief Constructs a Slab allocator.
        /// @param allocator Allocator for the internal arrays and pools.
        /// @param size Size of a single object in bytes.
        /// @param capacity Capacity per internal cache (Chunk size).
	constexpr Slab(Allocator& allocator, Ulen size, Ulen capacity)
            : caches_{allocator}
            , size_{size}
            , capacity_{capacity}
	{}

        /// @brief Loads a Slab from a stream.
	static Maybe<Slab> load(Allocator& allocator, Stream& stream);

        /// @brief Saves the Slab to a stream.
	Bool save(Stream& stream) const;

        /// @brief Destroys elements and deallocates memory.
        void destroy();

        /// @brief Allocates a new object. Automatically grows if necessary.
	Maybe<SlabRef> allocate();

        /// @brief Deallocates the object at the given reference.
	void deallocate(SlabRef slab_ref);

        /// @brief Access raw memory at the given reference.
	CTL_FORCEINLINE constexpr Uint8* operator[](SlabRef slab_ref) {
            const auto cache_idx = Uint32(slab_ref.index / capacity_);
            const auto cache_ref = Uint32(slab_ref.index % capacity_);
            return (*caches_[cache_idx])[PoolRef { cache_ref }];
	}

        /// @brief Access raw memory at the given reference (const).
	CTL_FORCEINLINE constexpr const Uint8* operator[](SlabRef slab_ref) const {
            const auto cache_idx = Uint32(slab_ref.index / capacity_);
            const auto cache_ref = Uint32(slab_ref.index % capacity_);
            return (*caches_[cache_idx])[PoolRef { cache_ref }];
	}
    private:
	Slab(Array<Maybe<Pool>>&& caches, Ulen size, Ulen capacity)
            : caches_{move(caches)}
            , size_{size}
            , capacity_{capacity}
	{}
	Array<Maybe<Pool>> caches_;
	Ulen               size_;
	Ulen               capacity_;
    };

} // namespace ctl

#endif // CTL_SLAB_H
