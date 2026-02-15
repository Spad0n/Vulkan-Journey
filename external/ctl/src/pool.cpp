#include "ctl/pool.hpp"
#include "ctl/slice.hpp"
#include "ctl/stream.hpp"

namespace ctl {

    // The serialized representation of the Pool
    struct PoolHeader {
	Uint8  magic[4]; // 'Pool'
	Uint32 version;
	Uint64 length;
	Uint64 size;
	Uint64 capacity;
    };
    // Following the header:
    // 	Uint32 used[PoolHeader::capacity / BITS]
    // 	Uint8  data[PoolHeader::size * PoolHeader::capacity]
    static_assert(sizeof(PoolHeader) == 32);

    Maybe<Pool> Pool::create(Allocator& allocator, Ulen size, Ulen capacity) {
	// Ensure capacity is a multiple of BITS
	capacity = ((capacity + (BITS - 1)) / BITS) * BITS;
	auto data = allocator.allocate<Uint8>(size * capacity, true);
	if (!data) {
            return {};
	}
	auto used = allocator.allocate<Word>(capacity / BITS, true);
	if (!used) {
            allocator.deallocate(data, size * capacity);
            return {};
	}
	return Pool {
            allocator,
            size,
            0_ulen,
            capacity,
            data,
            used
	};
    }

    Maybe<Pool> Pool::load(Allocator& allocator, Stream& stream) {
	PoolHeader header;
	if (stream.read(Slice{&header, 1}.cast<Uint8>()) != sizeof(header)) {
            return {};
	}
	if (Slice<const Uint8>{header.magic} != Slice{"pool"}.cast<const Uint8>()) {
            return {};
	}
	if (header.version != 1) {
            return {};
	}
	const auto n_words = header.capacity / BITS;
	const auto n_bytes = header.size * header.capacity;
	auto used = allocator.allocate<Word>(n_words, false);
	auto data = allocator.allocate<Uint8>(n_bytes, false);
	if (!used || !data) {
            allocator.deallocate(used, n_words);
            allocator.deallocate(data, n_bytes);
            return {};
	}
	if (stream.read(Slice{used, n_words}.cast<Uint8>()) != (n_words * sizeof(Word)) ||
	    stream.read(Slice{data, n_bytes}.cast<Uint8>()) != (n_bytes * sizeof(Uint8)))
            {
		allocator.deallocate(used, n_words);
		allocator.deallocate(data, n_bytes);
		return {};
            }
	return Pool {
            allocator,
            Ulen(header.size),
            Ulen(header.length),
            Ulen(header.capacity),
            data,
            used
	};
    }

    Bool Pool::save(Stream& stream) const {
	PoolHeader header = {
            .magic    = { 'p', 'o', 'o', 'l' },
            .version  = Uint64(1),
            .length   = Uint64(length_),
            .size     = Uint64(size_),
            .capacity = Uint64(capacity_),
	};

        auto h_slice = Slice{&header, 1}.cast<const Uint8>();
        if (stream.write(h_slice) != h_slice.length()) return false;
        
        auto u_slice = Slice{used_, capacity_ / BITS}.cast<const Uint8>();
        if (stream.write(u_slice) != u_slice.length()) return false;
        
        auto d_slice = Slice{data_, size_ * capacity_}.cast<const Uint8>();
        if (stream.write(d_slice) != d_slice.length()) return false;

        return true;
    }

    Pool::Pool(Pool&& other)
	: allocator_{other.allocator_}
	, size_{exchange(other.size_, 0)}
	, length_{exchange(other.length_, 0)}
	, capacity_{exchange(other.capacity_, 0)}
	, data_{exchange(other.data_, nullptr)}
	, used_{exchange(other.used_, nullptr)}
	, last_{exchange(other.last_, 0)}
    {
    }

#if defined(CTL_COMPILER_MSVC)

    typedef unsigned long DWORD;

    // Count the number of trailing zero bits in [value] which is the same as
    // giving the index to the first non-zero bit.
    static inline Uint32 count_trailing_zeros(Uint64 value) {
        DWORD trailing_zero = 0;
        if (_BitScanForward64(&trailing_zero, value)) {
            return trailing_zero;
        }
        return 64;
    }
#else
    static inline Uint32 count_trailing_zeros(Uint64 value) {
        return __builtin_ctzll(value);
    }
#endif

    Maybe<PoolRef> Pool::allocate() {
	const auto n_words = Uint32(capacity_ / BITS);
	const auto w_index = last_;
	if (auto scan = ~used_[w_index]) {
            auto b_index = count_trailing_zeros(scan);
            used_[w_index] |= Word(1) << b_index;
            length_++;
            return PoolRef { w_index * BITS + b_index };
	}
	for (Uint32 w_index = n_words - 1; w_index < n_words; w_index--) {
            if (auto scan = ~used_[w_index]) {
                auto b_index = count_trailing_zeros(scan);
                used_[w_index] |= Word(1) << b_index;
                length_++;
                last_ = w_index;
                return PoolRef { w_index * BITS + b_index };
            }
	}
	return {}; // Out of memory.
    }

    void Pool::deallocate(PoolRef ref) {
	const auto w_index = ref.index / BITS;
	const auto b_index = ref.index % BITS;
	used_[w_index] &= ~(Word(1) << b_index);
	length_--;
    }

} // namespace ctl
