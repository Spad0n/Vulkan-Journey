#ifndef CTL_PMR_BRIDGE_HPP
#define CTL_PMR_BRIDGE_HPP
#include "ctl/allocator.hpp"

#include <memory_resource>
#include <new>          // std::bad_alloc
#include <cstddef>      // std::size_t
#include <cstdint>      // std::uintptr_t

namespace ctl {

    /// @brief Adaptateur qui expose un ctl::Allocator comme std::pmr::memory_resource.
    ///
    /// Se passe ensuite directement à n'importe quel conteneur std::pmr::*
    /// ou à toute API qui accepte un memory_resource*.
    ///
    /// @note Précondition : le ctl::Allocator sous-jacent doit retourner des
    /// adresses alignées au moins sur 16 octets pour les allocations « normales ».
    /// C'est le cas pour SystemAllocator, TemporaryAllocator, ScratchAllocator
    /// et InlineAllocator (alignas(16)). Pour un ArenaAllocator construit sur
    /// une mémoire arbitraire, l'utilisateur doit garantir cette propriété.
    struct PmrAdapter final : std::pmr::memory_resource {
        explicit PmrAdapter(Allocator& backing) noexcept
            : backing_{backing}
        {}

        // Un adaptateur référence un Allocator unique : pas de copie ni de move
        // pour éviter d'avoir deux adaptateurs distincts pointant sur le même
        // backing sans que ce soit explicite.
        PmrAdapter(const PmrAdapter&)            = delete;
        PmrAdapter(PmrAdapter&&)                 = delete;
        PmrAdapter& operator=(const PmrAdapter&) = delete;
        PmrAdapter& operator=(PmrAdapter&&)      = delete;

        Allocator& backing() noexcept { return backing_; }

    protected:
        void* do_allocate(std::size_t bytes, std::size_t alignment) override {
            // CTL aligne implicitement à 16 octets (cf. Allocator::round).
            // Pour une demande > 16 octets d'alignement, on sur-alloue et on
            // stocke le pointeur original juste avant le pointeur aligné.
            if (alignment <= 16) {
                if (auto addr = backing_.alloc(bytes, /*zero=*/false)) {
                    return reinterpret_cast<void*>(addr);
                }
                throw std::bad_alloc{};
            }

            // Cas sur-aligné : on alloue bytes + alignment + sizeof(Header),
            // on aligne, et on stocke l'adresse d'origine + la taille totale
            // dans un en-tête juste avant le pointeur retourné, pour pouvoir
            // libérer correctement.
            const std::size_t pad   = alignment + sizeof(Header);
            const std::size_t total = bytes + pad;

            const auto raw = backing_.alloc(total, /*zero=*/false);
            if (!raw) throw std::bad_alloc{};

            const auto base    = static_cast<std::uintptr_t>(raw);
            const auto aligned = (base + pad) & ~(static_cast<std::uintptr_t>(alignment) - 1);

            // Stocke l'en-tête juste avant le pointeur retourné.
            auto* hdr = reinterpret_cast<Header*>(aligned - sizeof(Header));
            hdr->raw_addr  = raw;
            hdr->raw_total = total;
            return reinterpret_cast<void*>(aligned);
        }

        void do_deallocate(void* p, std::size_t bytes, std::size_t alignment) override {
            if (!p) return;

            if (alignment <= 16) {
                backing_.free(reinterpret_cast<Address>(p), bytes);
                return;
            }

            const auto aligned = reinterpret_cast<std::uintptr_t>(p);
            auto* hdr = reinterpret_cast<Header*>(aligned - sizeof(Header));
            backing_.free(hdr->raw_addr, hdr->raw_total);
        }

        bool do_is_equal(const std::pmr::memory_resource& other) const noexcept override {
            // Deux adaptateurs sont égaux ssi ils enveloppent le même Allocator.
            if (auto* o = dynamic_cast<const PmrAdapter*>(&other)) {
                return &backing_ == &o->backing_;
            }
            return false;
        }

    private:
        struct Header {
            Address     raw_addr;   // Adresse originale retournée par alloc()
            std::size_t raw_total;  // Taille totale réellement allouée
        };

        // Garanties pour que (aligned - sizeof(Header)) reste valable.
        static_assert(sizeof(Header) <= 16, "Header doit tenir dans 16 octets");
        static_assert(alignof(Header) <= 16, "alignof(Header) doit etre <= 16");

        Allocator& backing_;
    };

    /// @brief Adaptateur qui expose un std::pmr::memory_resource comme ctl::Allocator.
    ///
    /// Permet d'utiliser un memory_resource (par exemple monotonic_buffer_resource,
    /// unsynchronized_pool_resource, ou un resource custom) avec les conteneurs CTL.
    struct CtlFromPmr final : Allocator {
        explicit CtlFromPmr(std::pmr::memory_resource* mr) noexcept
            : mr_{mr}
        {}

        CtlFromPmr(const CtlFromPmr&)            = delete;
        CtlFromPmr(CtlFromPmr&&)                 = delete;
        CtlFromPmr& operator=(const CtlFromPmr&) = delete;
        CtlFromPmr& operator=(CtlFromPmr&&)      = delete;

        Address alloc(Ulen length, Bool zero) override {
#if __cpp_exceptions
            try {
                void* p = mr_->allocate(length, 16);
                const auto addr = reinterpret_cast<Address>(p);
                if (zero) memzero(addr, length);
                return addr;
            } catch (...) {
                return 0;  // Sémantique CTL : 0 sur échec.
            }
#else
            // Sans exceptions, on suppose que le memory_resource sous-jacent
            // termine le programme sur OOM (contrat habituel dans ce mode).
            void* p = mr_->allocate(length, 16);
            const auto addr = reinterpret_cast<Address>(p);
            if (zero) memzero(addr, length);
            return addr;
#endif
        }

        void free(Address addr, Ulen old_len) override {
            if (!addr) return;
            mr_->deallocate(reinterpret_cast<void*>(addr), old_len, 16);
        }

        void shrink(Address, Ulen, Ulen) override {
            // pmr ne supporte pas shrink in-place : no-op.
        }

        Address grow(Address old_addr, Ulen old_len, Ulen new_len, Bool zero) override {
            // pmr ne supporte pas grow in-place : alloc + copy + free.
            const auto new_addr = alloc(new_len, false);
            if (!new_addr) return 0;
            memcopy(new_addr, old_addr, old_len);
            if (zero && new_len > old_len) {
                memzero(new_addr + old_len, new_len - old_len);
            }
            free(old_addr, old_len);
            return new_addr;
        }

        std::pmr::memory_resource* resource() const noexcept { return mr_; }

    private:
        std::pmr::memory_resource* mr_;
    };

} // namespace ctl

#endif // CTL_PMR_BRIDGE_HPP
