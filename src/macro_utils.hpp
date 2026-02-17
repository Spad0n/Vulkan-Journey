#ifndef MACRO_UTILS_HPP
#define MACRO_UTILS_HPP

#define STATIC_LEN(x) (sizeof(x)/sizeof(x[0]))
#define MAX(a, b) ((a) > (b) ? (a) : (b))
#define MIN(a, b) ((a) < (b) ? (a) : (b))

template<typename F>
struct privDefer {
    F f;
    constexpr privDefer(F f) : f(f) {}
    ~privDefer() { f(); }
};

template<typename F>
privDefer<F> defer_func(F f) {
    return privDefer<F>(f);
}

#define DEFER_1(x, y) x##y
#define DEFER_2(x, y) DEFER_1(x, y)
#define DEFER_3(x)    DEFER_2(x, __COUNTER__)
#define defer(code)   auto DEFER_3(_defer_) = defer_func([&](){code;})

#endif // MACRO_UTILS_HPP
