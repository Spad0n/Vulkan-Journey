#ifndef CTL_PRIORITY_QUEUE_HPP
#define CTL_PRIORITY_QUEUE_HPP
#include "ctl/array.hpp"
#include "ctl/maybe.hpp"
#include "ctl/move.hpp"

namespace ctl {

    template<typename T>
    struct Less {
        constexpr Bool operator()(const T& a, const T& b) const {
            return a < b;
        }
    };

    template<typename T>
    constexpr void swap_items(T& a, T& b) {
        T temp = move(a);
        a = move(b);
        b = move(temp);
    }

    template<typename T, typename Compare = Less<T>>
    struct PriorityQueue {
        constexpr PriorityQueue(Allocator& allocator, Compare comp = Compare{})
            : queue_{allocator}
            , less_{comp}
        {}

        PriorityQueue(PriorityQueue&& other)
            : queue_{move(other.queue_)}
            , less_{move(other.less_)}
        {}

        PriorityQueue& operator=(PriorityQueue&& other) {
            queue_ = move(other.queue_);
            less_ = move(other.less_);
            return *this;
        }

        PriorityQueue(const PriorityQueue&) = delete;
        PriorityQueue& operator=(const PriorityQueue&) = delete;

        Bool reserve(Ulen capacity) {
            return queue_.reserve(capacity);
        }

        void clear() {
            queue_.clear();
        }

        void destroy() {
            queue_.reset();
        }

        [[nodiscard]] CTL_FORCEINLINE Ulen length() const { return queue_.length(); }
        [[nodiscard]] CTL_FORCEINLINE Ulen capacity() const { return queue_.capacity(); }
        [[nodiscard]] CTL_FORCEINLINE Bool is_empty() const { return queue_.is_empty(); }

        Bool push(T&& value) requires MoveConstructible<T> {
            if (!queue_.push_back(move(value))) return false;
            shift_up(queue_.length() - 1);
            return true;
        }

        Bool push(const T& value) requires CopyConstructible<T> {
            if (!queue_.push_back(value)) return false;
            shift_up(queue_.length() - 1);
            return true;
        }

        Maybe<T> pop() {
            if (queue_.is_empty()) return {};

            Ulen n = queue_.length() - 1;
            swap_items(queue_[0], queue_[n]);
            shift_down(0, n);

            T val = move(queue_[n]);
            queue_.pop_back();
            return val;
        }

        T* peek() {
            if (queue_.is_empty()) return nullptr;
            return &queue_[0];
        }

        const T* peek() const {
            if (queue_.is_empty()) return nullptr;
            return &queue_[0];
        }

        Maybe<T> remove(Ulen i) {
            Ulen n = queue_.length();
            if (i >= n) return {};

            swap_items(queue_[i], queue_[n - 1]);
            shift_down(i, n - 1);
            shift_up(i);

            T val = move(queue_[n - 1]);
            queue_.pop_back();
            return val;
        }

        void fix(Ulen i) {
            if (!shift_down(i, queue_.length())) {
                shift_up(i);
            }
        }

    private:
        Array<T> queue_;
        Compare  less_;

        void shift_up(Ulen j) {
            while (j > 0) {
                Ulen i = (j - 1) / 2;
                if (!less_(queue_[j], queue_[i])) {
                    break;
                }
                swap_items(queue_[i], queue_[j]);
                j = i;
            }
        }

        Bool shift_down(Ulen i0, Ulen n) {
            Ulen i = i0;
            while (true) {
                Ulen j1 = 2 * i + 1;
                if (j1 >= n) break;
                
                Ulen j = j1;
                Ulen j2 = j1 + 1;
                
                if (j2 < n && less_(queue_[j2], queue_[j1])) {
                    j = j2;
                }
                if (!less_(queue_[j], queue_[i])) {
                    break;
                }
                
                swap_items(queue_[i], queue_[j]);
                i = j;
            }
            return i > i0;
        }
    };

} // namespace ctl

#endif // CTL_PRIORITY_QUEUE_HPP
