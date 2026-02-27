#include "ctl/slab.hpp"
#include "ctl/stream.hpp"

namespace ctl {

    struct SlabHeader {
	Uint8  magic[4]; // slab
	Uint32 version;
	Uint64 size;
	Uint64 capacity;
	Uint64 caches;
    };
    // Following the header:
    // 	Uint32 used[SlabHeader::capacity / 32]
    // 	Pool   pools[]
    //
    // Only pools that are valid are stored. Active pools are indicated by the used
    // bitset. That is ((used[i/32] & (1 << (i%32)) != 0 indicates if pool i exists.
    Maybe<Slab> Slab::load(Allocator& allocator, Stream& stream) {
	SlabHeader header;
	if (stream.read(Slice{&header, 1}.cast<Uint8>()) != sizeof(header)) {
            return {};
	}
	if (Slice<const Uint8>{header.magic} != Slice{"slab"}.cast<const Uint8>()) {
            return {};
	}
	if (header.version != 1) {
            return {};
	}
	ScratchAllocator<1024> scratch{allocator};
	auto n_words = header.capacity / 32;
	auto used = scratch.allocate<Uint32>(n_words, true);
	if (!used) {
            return {};
	}
	if (stream.read(Slice{used, n_words}.cast<Uint8>()) != (n_words * sizeof(Uint32))) {
            return {};
	}
	auto n_caches = Ulen(header.caches);
	Array<Maybe<Pool>> caches{allocator};
	if (!caches.resize(n_caches)) {
            return {};
	}
	for (Ulen i = 0; i < n_caches; i++) {
            const auto w_index = Uint32(i / 32);
            const auto b_index = Uint32(i % 32);
            if ((used[w_index] & (1_u32 << b_index)) != 0) {
                if (auto cache = Pool::load(allocator, stream)) {
                    caches[i] = move(*cache);
                } else {
                    return {};
                }
            }
	}
	return Slab {
            move(caches),
            Ulen(header.size),
            Ulen(header.capacity)
	};
    }

    Bool Slab::save(Stream& stream) const {
	SlabHeader header = {
            .magic    = { 's', 'l', 'a', 'b' },
            .version  = 1,
            .size     = Uint64(size_),
            .capacity = Uint64(capacity_),
            .caches   = Uint64(caches_.length()),
	};
	ScratchAllocator<1024> scratch{caches_.allocator()};
	auto n_words = capacity_ / 32;
	auto used = scratch.allocate<Uint32>(n_words, true);
	if (!used) {
            return false;
	}
	Ulen i = 0;
	for (const auto& cache : caches_) {
            if (cache) {
                const auto w_index = Uint32(i / 32);
                const auto b_index = Uint32(i % 32);
                used[w_index] |= 1_u32 << b_index;
            }
            i++;
	}
        auto h_slice = Slice{&header, 1}.cast<const Uint8>();
        if (stream.write(h_slice) != h_slice.length()) return false;

        auto u_slice = Slice{used, n_words}.cast<const Uint8>();
        if (stream.write(u_slice) != u_slice.length()) return false;

	for (const auto& cache : caches_) {
            if (cache && !cache->save(stream)) {
                return false;
            }
	}
	return true;
    }

    Maybe<SlabRef> Slab::allocate() {
	const auto n_caches = caches_.length();
	for (Ulen i = n_caches - 1; i < n_caches; i--) {
            if (auto& cache = caches_[i]; cache.is_valid()) {
                if (auto c_ref = cache->allocate()) {
                    return SlabRef { Uint32(i * capacity_) + c_ref->index };
                }
            }
	}
	auto pool = Pool::create(caches_.allocator(), size_, capacity_);
	if (!pool) {
            return {};
	}
	// Search for an empty Pool in the caches array
	for (auto& cache : caches_) {
            if (!cache.is_valid()) {
                cache = move(*pool);
                return allocate();
            }
	}
	// No empty Pool in caches array, append a new Pool.
	if (!caches_.push_back(move(*pool))) {
            return {};
	}
	return allocate();
    }

    void Slab::deallocate(SlabRef slab_ref) {
        const auto cache_idx = Uint32(slab_ref.index / capacity_);
        const auto cache_ref = Uint32(slab_ref.index % capacity_);
    
        caches_[cache_idx]->deallocate(PoolRef { cache_ref });

        if (caches_[cache_idx]->is_empty()) {
            caches_[cache_idx].reset(); 
        }

        while (!caches_.is_empty() && !caches_.last().is_valid()) {
            caches_.pop_back();
        }
    }

} // namespace ctl
