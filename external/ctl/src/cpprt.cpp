#include "ctl/types.hpp"

using namespace ctl;

// libstdc++ ABI implementation.
#if !defined(CTL_COMPILER_MSVC)
struct Guard {
    Uint8 done;
    Uint8 pending;
    Uint8 padding[62];
};
static_assert(sizeof(Guard) == 64);

void operator delete(void*) noexcept {
    // See comment below
}
void operator delete(void*, unsigned long) noexcept {
    // When a base class contains a virtual destructor, the compiler will generate
    // two destructors for a derived class. The regular destructor and a special
    // destructor called a "deleting destructor" which is called when "delete" is
    // used. This deleting destructor will call this operator delete. We never use
    // "delete" though and so these deleting destructors are never called. The ABI
    // requires they be generated though because the compiler cannot tell that a
    // client won't use delete.
}

extern "C" {

    int __cxa_guard_acquire(Guard* guard) {
	if (guard->done) {
            return 0;
	}
	if (guard->pending) {
            *(volatile int *)0 = 0;
	}
	guard->pending = 1;
	return 1;
    }

    void __cxa_guard_release(Guard* guard) {
	guard->done = 1;
    }

    void __cxa_pure_virtual() {
	// No-op
    }

} // extern "C"

#endif
