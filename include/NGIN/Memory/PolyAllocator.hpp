#pragma once

#include <NGIN/Memory/AllocatorConcept.hpp>

namespace NGIN::Memory
{

    class PolyAllocator
    {
    public:
        PolyAllocator() = delete;

        template<AllocatorConcept A>
        explicit PolyAllocator(A a) noexcept
            : obj_(reinterpret_cast<void*>(new A(std::move(a)))), vt_({// allocate
                                                                       [](void* o, std::size_t n, std::size_t al) noexcept -> void* {
                                                                           return static_cast<A*>(o)->Allocate(n, al);
                                                                       },
                                                                       // deallocate
                                                                       [](void* o, void* p, std::size_t n, std::size_t al) noexcept {
                                                                           static_cast<A*>(o)->Deallocate(p, n, al);
                                                                       },
                                                                       // maxsize
                                                                       [](const void* o) noexcept -> std::size_t {
                                                                           return static_cast<const A*>(o)->MaxSize();
                                                                       },
                                                                       // remaining
                                                                       [](const void* o) noexcept -> std::size_t {
                                                                           return static_cast<const A*>(o)->Remaining();
                                                                       },
                                                                       // owns
                                                                       [](const void* o, const void* p) noexcept -> bool {
                                                                           return static_cast<const A*>(o)->Owns(p);
                                                                       },
                                                                       // destroy
                                                                       [](void* o) noexcept {
                                                                           delete static_cast<A*>(o);
                                                                       }})
        {}

        PolyAllocator(const PolyAllocator&)            = delete;
        PolyAllocator& operator=(const PolyAllocator&) = delete;

        PolyAllocator(PolyAllocator&& rhs) noexcept : obj_(rhs.obj_), vt_(rhs.vt_) { rhs.obj_ = nullptr; }
        PolyAllocator& operator=(PolyAllocator&& rhs) noexcept
        {
            if (this != &rhs)
            {
                destroy_();
                obj_     = rhs.obj_;
                vt_      = rhs.vt_;
                rhs.obj_ = nullptr;
            }
            return *this;
        }

        ~PolyAllocator() { destroy_(); }

        // Satisfy AllocatorConcept
        void*       Allocate(std::size_t n, std::size_t a) noexcept { return vt_.alloc(obj_, n, a); }
        void        Deallocate(void* p, std::size_t n, std::size_t a) noexcept { vt_.dealloc(obj_, p, n, a); }
        std::size_t MaxSize() const noexcept { return vt_.maxsize(obj_); }
        std::size_t Remaining() const noexcept { return vt_.remaining(obj_); }
        bool        Owns(const void* p) const noexcept { return vt_.owns(obj_, p); }

    private:
        struct VTable
        {
            void* (*alloc)(void*, std::size_t, std::size_t) noexcept;
            void (*dealloc)(void*, void*, std::size_t, std::size_t) noexcept;
            std::size_t (*maxsize)(const void*) noexcept;
            std::size_t (*remaining)(const void*) noexcept;
            bool (*owns)(const void*, const void*) noexcept;
            void (*destroy)(void*) noexcept;
        } vt_ {};

        void* obj_ {nullptr};
        void  destroy_() noexcept
        {
            if (obj_)
                vt_.destroy(obj_);
            obj_ = nullptr;
        }
    };

    static_assert(AllocatorConcept<PolyAllocator>);

}// namespace NGIN::Memory
