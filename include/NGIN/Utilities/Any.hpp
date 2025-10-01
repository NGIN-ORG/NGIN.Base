/// @file Any.hpp
/// @brief Small-buffer-optimized type-erased container with customizable allocators.
#pragma once

#include <NGIN/Hashing/FNV.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Meta/TypeName.hpp>
#include <NGIN/Primitives.hpp>

#include <any>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace NGIN::Utilities
{
    namespace detail
    {
        /// <summary>Compute FNV-1a64 hashes for type identifiers using qualified names.</summary>
        struct AnyDefaultTypeIdPolicy
        {
            template<typename T>
            static constexpr UInt64 Compute() noexcept
            {
                using Base = std::remove_cv_t<std::remove_reference_t<T>>;
                if constexpr (std::is_same_v<Base, void>)
                {
                    return 0u;
                }
                else
                {
                    constexpr std::string_view name = NGIN::Meta::TypeName<Base>::qualifiedName;
                    return NGIN::Hashing::FNV1a64(name);
                }
            }
        };

        template<std::size_t SboSize>
        struct AnyStorage
        {
            alignas(std::max_align_t) std::byte inlineBytes[SboSize > 0 ? SboSize : 1] {};
            void* heapPtr {nullptr};
        };

        template<typename TypeIdPolicy, typename T>
        constexpr UInt64 AnyTypeIdOf() noexcept
        {
            return TypeIdPolicy::template Compute<std::remove_cv_t<std::remove_reference_t<T>>>();
        }

        template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
        struct AnyTypeDescriptor
        {
            using Storage       = AnyStorage<SboSize>;
            using CopyFn        = void (*)(Storage&, const Storage&, Allocator&);
            using DestroyFn     = void (*)(Storage&, Allocator&) noexcept;
            using MoveFn        = void (*)(Storage&, Storage&);
            using AccessFn      = void* (*) (Storage&) noexcept;
            using ConstAccessFn = const void* (*) (const Storage&) noexcept;

            UInt64        typeId {0};
            UIntSize      sizeBytes {0};
            UIntSize      alignment {alignof(std::max_align_t)};
            CopyFn        copy {nullptr};
            DestroyFn     destroy {nullptr};
            MoveFn        move {nullptr};
            AccessFn      access {nullptr};
            ConstAccessFn accessConst {nullptr};
            bool          storesInline {true};
        };

        template<typename Stored, std::size_t SboSize, class Allocator, class TypeIdPolicy>
        struct AnyDescriptorProvider
        {
            using Storage    = AnyStorage<SboSize>;
            using Descriptor = AnyTypeDescriptor<SboSize, Allocator, TypeIdPolicy>;

            static constexpr bool FitsInline = (sizeof(Stored) <= SboSize) && (alignof(Stored) <= alignof(std::max_align_t));

            static void Destroy(Storage& storage, Allocator& allocator) noexcept
            {
                if constexpr (!std::is_trivially_destructible_v<Stored>)
                {
                    if constexpr (FitsInline)
                    {
                        auto* ptr = std::launder(reinterpret_cast<Stored*>(storage.inlineBytes));
                        ptr->~Stored();
                    }
                    else
                    {
                        if (storage.heapPtr != nullptr)
                        {
                            std::destroy_at(static_cast<Stored*>(storage.heapPtr));
                        }
                    }
                }
                if constexpr (!FitsInline)
                {
                    if (storage.heapPtr != nullptr)
                    {
                        allocator.Deallocate(storage.heapPtr, sizeof(Stored), alignof(Stored));
                        storage.heapPtr = nullptr;
                    }
                }
            }

            static void Move(Storage& dst, Storage& src)
            {
                if constexpr (FitsInline)
                {
                    auto* srcPtr = std::launder(reinterpret_cast<Stored*>(src.inlineBytes));
                    auto* dstPtr = reinterpret_cast<Stored*>(dst.inlineBytes);
                    if constexpr (std::is_move_constructible_v<Stored>)
                    {
                        std::construct_at(dstPtr, std::move(*srcPtr));
                    }
                    else if constexpr (std::is_copy_constructible_v<Stored>)
                    {
                        std::construct_at(dstPtr, *srcPtr);
                    }
                    else if constexpr (std::is_trivially_copyable_v<Stored>)
                    {
                        std::memcpy(dst.inlineBytes, src.inlineBytes, sizeof(Stored));
                    }
                    else
                    {
                        static_assert(std::is_move_constructible_v<Stored> || std::is_copy_constructible_v<Stored> || std::is_trivially_copyable_v<Stored>,
                                      "Stored type must be movable, copyable, or trivially copyable to reside in Any.");
                    }
                    if constexpr (!std::is_trivially_destructible_v<Stored>)
                    {
                        std::destroy_at(srcPtr);
                    }
                    std::memset(src.inlineBytes, 0, sizeof(src.inlineBytes));
                }
                else
                {
                    dst.heapPtr = src.heapPtr;
                    src.heapPtr = nullptr;
                }
            }

            static void Copy(Storage& dst, const Storage& src, Allocator& allocator)
            {
                if constexpr (FitsInline)
                {
                    const auto* srcPtr = std::launder(reinterpret_cast<const Stored*>(src.inlineBytes));
                    auto*       dstPtr = reinterpret_cast<Stored*>(dst.inlineBytes);
                    if constexpr (std::is_copy_constructible_v<Stored>)
                    {
                        std::construct_at(dstPtr, *srcPtr);
                    }
                    else if constexpr (std::is_trivially_copyable_v<Stored>)
                    {
                        std::memcpy(dst.inlineBytes, src.inlineBytes, sizeof(Stored));
                    }
                    else
                    {
                        throw std::bad_any_cast();
                    }
                }
                else
                {
                    const auto* srcPtr = static_cast<const Stored*>(src.heapPtr);
                    void*       mem    = allocator.Allocate(sizeof(Stored), alignof(Stored));
                    if (mem == nullptr)
                        throw std::bad_alloc();
                    try
                    {
                        if constexpr (std::is_copy_constructible_v<Stored>)
                        {
                            std::construct_at(static_cast<Stored*>(mem), *srcPtr);
                        }
                        else if constexpr (std::is_trivially_copyable_v<Stored>)
                        {
                            std::memcpy(mem, srcPtr, sizeof(Stored));
                        }
                        else
                        {
                            allocator.Deallocate(mem, sizeof(Stored), alignof(Stored));
                            throw std::bad_any_cast();
                        }
                    } catch (...)
                    {
                        allocator.Deallocate(mem, sizeof(Stored), alignof(Stored));
                        throw;
                    }
                    dst.heapPtr = mem;
                }
            }

            static void* Access(Storage& storage) noexcept
            {
                if constexpr (FitsInline)
                {
                    return static_cast<void*>(std::launder(reinterpret_cast<Stored*>(storage.inlineBytes)));
                }
                else
                {
                    return storage.heapPtr;
                }
            }

            static const void* AccessConst(const Storage& storage) noexcept
            {
                if constexpr (FitsInline)
                {
                    return static_cast<const void*>(std::launder(reinterpret_cast<const Stored*>(storage.inlineBytes)));
                }
                else
                {
                    return storage.heapPtr;
                }
            }

            static inline constexpr Descriptor descriptor {
                    .typeId       = AnyTypeIdOf<TypeIdPolicy, Stored>(),
                    .sizeBytes    = sizeof(Stored),
                    .alignment    = alignof(Stored),
                    .copy         = &Copy,
                    .destroy      = &Destroy,
                    .move         = &Move,
                    .access       = &Access,
                    .accessConst  = &AccessConst,
                    .storesInline = FitsInline,
            };
        };

        template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
        using AnyDescriptorBase = AnyTypeDescriptor<SboSize, Allocator, TypeIdPolicy>;

        template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
        class AnyViewBase
        {
        public:
            using Descriptor = AnyDescriptorBase<SboSize, Allocator, TypeIdPolicy>;

            constexpr AnyViewBase(const void* data, const Descriptor* descriptor) noexcept
                : m_data(data), m_descriptor(descriptor)
            {
            }

            [[nodiscard]] constexpr UInt64 TypeId() const noexcept
            {
                return m_descriptor ? m_descriptor->typeId : 0u;
            }

            [[nodiscard]] constexpr UIntSize Size() const noexcept
            {
                return m_descriptor ? m_descriptor->sizeBytes : 0u;
            }

            template<typename T>
            [[nodiscard]] const T* TryCast() const noexcept
            {
                using Base = std::remove_cv_t<std::remove_reference_t<T>>;
                if (!m_descriptor || m_descriptor->typeId != AnyTypeIdOf<TypeIdPolicy, Base>())
                    return nullptr;
                return static_cast<const Base*>(m_data);
            }

            template<typename T>
            const T& Cast() const
            {
                auto* ptr = TryCast<T>();
                if (ptr == nullptr)
                    throw std::bad_any_cast();
                return *ptr;
            }

        protected:
            const void*       m_data {nullptr};
            const Descriptor* m_descriptor {nullptr};
        };
    }// namespace detail

    /// <summary>Mutable view into an `Any` payload.</summary>
    template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
    class AnyView : public detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>
    {
    public:
        using Base       = detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>;
        using Descriptor = typename Base::Descriptor;

        constexpr AnyView(void* data, const Descriptor* descriptor) noexcept
            : detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>(data, descriptor)
        {
        }

        template<typename T>
        [[nodiscard]] T* TryCast() const noexcept
        {
            using BaseT = std::remove_cv_t<std::remove_reference_t<T>>;
            if (!this->m_descriptor || this->m_descriptor->typeId != detail::AnyTypeIdOf<TypeIdPolicy, BaseT>())
                return nullptr;
            return static_cast<BaseT*>(const_cast<void*>(this->m_data));
        }

        template<typename T>
        T& Cast() const
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }
    };

    /// <summary>Immutable view into an `Any` payload.</summary>
    template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
    class ConstAnyView : public detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>
    {
    public:
        using detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::AnyViewBase;

        template<typename T>
        [[nodiscard]] const T* TryCast() const noexcept
        {
            return detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::template TryCast<T>();
        }

        template<typename T>
        const T& Cast() const
        {
            return detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::template Cast<T>();
        }
    };

    /// <summary>
    /// Small-buffer-optimized type-erased container with allocator and visit support.
    /// </summary>
    template<std::size_t SboSize = 32,
             class Allocator     = NGIN::Memory::SystemAllocator,
             class TypeIdPolicy  = detail::AnyDefaultTypeIdPolicy>
        requires NGIN::Memory::AllocatorConcept<Allocator>
    class Any
    {
    public:
        using Storage    = detail::AnyStorage<SboSize>;
        using Descriptor = detail::AnyTypeDescriptor<SboSize, Allocator, TypeIdPolicy>;
        using View       = AnyView<SboSize, Allocator, TypeIdPolicy>;
        using ConstView  = ConstAnyView<SboSize, Allocator, TypeIdPolicy>;
        using TypeId     = UInt64;

        static constexpr TypeId VOID_TYPE_ID = 0u;

        constexpr Any() noexcept = default;

        explicit constexpr Any(const Allocator& allocator) noexcept(std::is_nothrow_copy_constructible_v<Allocator>)
            : m_allocator(allocator)
        {
        }

        Any(const Any& other)
            : m_allocator(other.m_allocator)
        {
            if (other.m_descriptor != nullptr)
            {
                other.m_descriptor->copy(m_storage, other.m_storage, m_allocator);
                m_descriptor = other.m_descriptor;
            }
        }

        Any(Any&& other) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
            : m_allocator(std::move(other.m_allocator))
        {
            MoveFrom(std::move(other));
        }

        template<typename T>
            requires(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, Any>)
        explicit Any(T&& value)
        {
            Emplace<std::remove_cv_t<std::remove_reference_t<T>>>(std::forward<T>(value));
        }

        template<typename T, typename... Args>
            requires(!std::is_same_v<std::remove_cv_t<T>, Any>)
        explicit Any(std::in_place_type_t<T>, Args&&... args)
        {
            Emplace<T>(std::forward<Args>(args)...);
        }

        ~Any()
        {
            Reset();
        }

        Any& operator=(const Any& other)
        {
            if (this == &other)
                return *this;
            Any copy(other);
            *this = std::move(copy);
            return *this;
        }

        Any& operator=(Any&& other) noexcept(std::is_nothrow_move_assignable_v<Allocator>)
        {
            if (this == &other)
                return *this;
            Reset();
            if constexpr (std::is_move_assignable_v<Allocator>)
            {
                m_allocator = std::move(other.m_allocator);
            }
            else if constexpr (std::is_copy_assignable_v<Allocator>)
            {
                m_allocator = other.m_allocator;
            }
            MoveFrom(std::move(other));
            return *this;
        }

        template<typename T>
            requires(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, Any>)
        Any& operator=(T&& value)
        {
            Emplace<std::remove_cv_t<std::remove_reference_t<T>>>(std::forward<T>(value));
            return *this;
        }

        template<typename T, typename... Args>
            requires(!std::is_same_v<std::remove_cv_t<T>, Any>)
        T& Emplace(Args&&... args)
        {
            using Stored               = std::remove_cv_t<T>;
            constexpr auto& descriptor = detail::AnyDescriptorProvider<Stored, SboSize, Allocator, TypeIdPolicy>::descriptor;
            Reset();
            void* target = nullptr;
            if (descriptor.storesInline)
            {
                target = static_cast<void*>(m_storage.inlineBytes);
            }
            else
            {
                target = m_allocator.Allocate(descriptor.sizeBytes, descriptor.alignment);
                if (target == nullptr)
                    throw std::bad_alloc();
                m_storage.heapPtr = target;
            }
            try
            {
                std::construct_at(static_cast<Stored*>(target), std::forward<Args>(args)...);
                m_descriptor = &descriptor;
            } catch (...)
            {
                if (!descriptor.storesInline && target != nullptr)
                {
                    m_allocator.Deallocate(target, descriptor.sizeBytes, descriptor.alignment);
                    m_storage.heapPtr = nullptr;
                }
                throw;
            }
            return *static_cast<Stored*>(target);
        }

        void Reset() noexcept
        {
            if (m_descriptor != nullptr)
            {
                m_descriptor->destroy(m_storage, m_allocator);
                m_descriptor      = nullptr;
                m_storage.heapPtr = nullptr;
            }
        }

        [[nodiscard]] bool HasValue() const noexcept
        {
            return m_descriptor != nullptr;
        }

        [[nodiscard]] bool IsInline() const noexcept
        {
            return m_descriptor != nullptr && m_descriptor->storesInline;
        }

        [[nodiscard]] TypeId GetTypeId() const noexcept
        {
            return m_descriptor ? m_descriptor->typeId : VOID_TYPE_ID;
        }

        [[nodiscard]] UIntSize Size() const noexcept
        {
            return m_descriptor ? m_descriptor->sizeBytes : 0u;
        }

        [[nodiscard]] UIntSize Alignment() const noexcept
        {
            return m_descriptor ? m_descriptor->alignment : alignof(std::max_align_t);
        }

        template<typename T>
        [[nodiscard]] bool Is() const noexcept
        {
            if (!m_descriptor)
                return false;
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            return m_descriptor->typeId == detail::AnyTypeIdOf<TypeIdPolicy, Base>();
        }

        template<typename T>
        [[nodiscard]] T* TryCast() noexcept
        {
            if (!m_descriptor)
                return nullptr;
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            if (m_descriptor->typeId != detail::AnyTypeIdOf<TypeIdPolicy, Base>())
                return nullptr;
            return static_cast<Base*>(m_descriptor->access(m_storage));
        }

        template<typename T>
        [[nodiscard]] const T* TryCast() const noexcept
        {
            if (!m_descriptor)
                return nullptr;
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            if (m_descriptor->typeId != detail::AnyTypeIdOf<TypeIdPolicy, Base>())
                return nullptr;
            return static_cast<const Base*>(m_descriptor->accessConst(m_storage));
        }

        template<typename T>
        T& Cast()
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }

        template<typename T>
        const T& Cast() const
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }

        template<typename Fn>
        decltype(auto) Visit(Fn&& fn)
        {
            if (!m_descriptor)
                throw std::logic_error("NGIN::Utilities::Any::Visit requires a value");
            View view {m_descriptor->access(m_storage), m_descriptor};
            return std::invoke(std::forward<Fn>(fn), view);
        }

        template<typename Fn>
        decltype(auto) Visit(Fn&& fn) const
        {
            if (!m_descriptor)
                throw std::logic_error("NGIN::Utilities::Any::Visit requires a value");
            ConstView view {m_descriptor->accessConst(m_storage), m_descriptor};
            return std::invoke(std::forward<Fn>(fn), view);
        }

        [[nodiscard]] View MakeView() noexcept
        {
            return View {m_descriptor ? m_descriptor->access(m_storage) : nullptr, m_descriptor};
        }

        [[nodiscard]] ConstView MakeView() const noexcept
        {
            return ConstView {m_descriptor ? m_descriptor->accessConst(m_storage) : nullptr, m_descriptor};
        }

        [[nodiscard]] Allocator& GetAllocator() noexcept
        {
            return m_allocator;
        }

        [[nodiscard]] const Allocator& GetAllocator() const noexcept
        {
            return m_allocator;
        }

        /// <summary>Mutable pointer to the stored object, or nullptr when empty.</summary>
        [[nodiscard]] void* Data() noexcept
        {
            if (m_descriptor == nullptr)
                return nullptr;
            return m_descriptor->access(m_storage);
        }

        /// <summary>Const pointer to the stored object, or nullptr when empty.</summary>
        [[nodiscard]] const void* Data() const noexcept
        {
            if (m_descriptor == nullptr)
                return nullptr;
            return m_descriptor->accessConst(m_storage);
        }

        static Any MakeVoid() noexcept
        {
            return Any {};
        }

    private:
        void MoveFrom(Any&& other)
        {
            m_descriptor = other.m_descriptor;
            if (m_descriptor == nullptr)
                return;
            if (m_descriptor->storesInline)
            {
                m_descriptor->move(m_storage, other.m_storage);
            }
            else
            {
                m_storage.heapPtr       = other.m_storage.heapPtr;
                other.m_storage.heapPtr = nullptr;
            }
            other.m_descriptor = nullptr;
        }

        Storage           m_storage {};
        const Descriptor* m_descriptor {nullptr};
        Allocator         m_allocator {};
    };
}// namespace NGIN::Utilities
