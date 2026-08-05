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

            static constexpr bool FitsInline =
                    (sizeof(Stored) <= SboSize) && (alignof(Stored) <= alignof(std::max_align_t));

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
                        static_assert(std::is_move_constructible_v<Stored> ||
                                              std::is_copy_constructible_v<Stored> ||
                                              std::is_trivially_copyable_v<Stored>,
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

            constexpr AnyViewBase() noexcept = default;

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

    /// @brief Non-owning mutable view of an `Any` payload.
    /// @tparam SboSize Inline storage size used by the viewed `Any` type.
    /// @tparam Allocator Allocator used by the viewed `Any` type.
    /// @tparam TypeIdPolicy Policy used to identify stored types.
    template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
    class AnyView : public detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>
    {
    public:
        using Base       = detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>;
        using Descriptor = typename Base::Descriptor;

        /// @brief Constructs an empty view.
        constexpr AnyView() noexcept : Base(nullptr, nullptr) {}

        /// @brief Constructs a view from payload data and its type descriptor.
        /// @param data Mutable payload address, or `nullptr` for an empty view.
        /// @param descriptor Descriptor associated with `data`, or `nullptr` for an empty view.
        constexpr AnyView(void* data, const Descriptor* descriptor) noexcept
            : detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>(data, descriptor)
        {
        }

        /// @brief Returns the payload as `T` when its type identifier matches.
        /// @tparam T Requested payload type.
        /// @return Pointer to the payload, or `nullptr` on a type mismatch or empty view.
        template<typename T>
        [[nodiscard]] T* TryCast() const noexcept
        {
            using BaseT = std::remove_cv_t<std::remove_reference_t<T>>;
            if (!this->m_descriptor || this->m_descriptor->typeId != detail::AnyTypeIdOf<TypeIdPolicy, BaseT>())
                return nullptr;
            return static_cast<BaseT*>(const_cast<void*>(this->m_data));
        }

        /// @brief Returns the payload as `T`.
        /// @tparam T Requested payload type.
        /// @throws std::bad_any_cast If the view is empty or contains another type.
        template<typename T>
        T& Cast() const
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }
    };

    /// @brief Non-owning immutable view of an `Any` payload.
    /// @tparam SboSize Inline storage size used by the viewed `Any` type.
    /// @tparam Allocator Allocator used by the viewed `Any` type.
    /// @tparam TypeIdPolicy Policy used to identify stored types.
    template<std::size_t SboSize, class Allocator, class TypeIdPolicy>
    class ConstAnyView : public detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>
    {
    public:
        using detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::AnyViewBase;

        /// @brief Constructs an empty view.
        constexpr ConstAnyView() noexcept : detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>(nullptr, nullptr) {}

        /// @brief Returns the payload as `T` when its type identifier matches.
        /// @tparam T Requested payload type.
        /// @return Pointer to the payload, or `nullptr` on a type mismatch or empty view.
        template<typename T>
        [[nodiscard]] const T* TryCast() const noexcept
        {
            return detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::template TryCast<T>();
        }

        /// @brief Returns the payload as `T`.
        /// @tparam T Requested payload type.
        /// @throws std::bad_any_cast If the view is empty or contains another type.
        template<typename T>
        const T& Cast() const
        {
            return detail::AnyViewBase<SboSize, Allocator, TypeIdPolicy>::template Cast<T>();
        }
    };

    /// @brief Owning small-buffer-optimized type-erased value.
    /// @details Copying an `Any` that holds a non-copyable, non-trivially-copyable type throws
    /// `std::bad_any_cast`.
    /// @tparam SboSize Number of bytes reserved for inline payload storage.
    /// @tparam Allocator Allocator used for payloads that do not fit inline.
    /// @tparam TypeIdPolicy Policy that maps payload types to stable identifiers.
    template<std::size_t SboSize = 32,
             class Allocator     = NGIN::Memory::SystemAllocator,
             class TypeIdPolicy  = detail::AnyDefaultTypeIdPolicy>
        requires NGIN::Memory::AllocatorConcept<Allocator>
    class Any
    {
    public:
        /// @brief Storage representation used by this specialization.
        using Storage = detail::AnyStorage<SboSize>;

        /// @brief Runtime payload descriptor used by this specialization.
        using Descriptor = detail::AnyTypeDescriptor<SboSize, Allocator, TypeIdPolicy>;

        /// @brief Mutable non-owning payload view.
        using View = AnyView<SboSize, Allocator, TypeIdPolicy>;

        /// @brief Immutable non-owning payload view.
        using ConstView = ConstAnyView<SboSize, Allocator, TypeIdPolicy>;

        /// @brief Type used for runtime payload identifiers.
        using TypeId = UInt64;

        /// @brief Type identifier returned when no value is stored.
        static constexpr TypeId VOID_TYPE_ID = 0u;

        /// @brief Constructs an empty value with a default-constructed allocator.
        constexpr Any() noexcept = default;

        /// @brief Constructs an empty value with a specific allocator.
        /// @param allocator Allocator copied into the container.
        explicit constexpr Any(const Allocator& allocator) noexcept(std::is_nothrow_copy_constructible_v<Allocator>)
            : m_allocator(allocator)
        {
        }

        /// @brief Copies a stored value and its allocator.
        /// @param other Value to copy.
        /// @throws std::bad_any_cast If the stored type cannot be copied.
        Any(const Any& other)
            : m_allocator(other.m_allocator)
        {
            if (other.m_descriptor != nullptr)
            {
                other.m_descriptor->copy(m_storage, other.m_storage, m_allocator);
                m_descriptor = other.m_descriptor;
            }
        }

        /// @brief Moves a stored value and its allocator, leaving `other` empty.
        /// @param other Value to consume.
        Any(Any&& other) noexcept(std::is_nothrow_move_constructible_v<Allocator>)
            : m_allocator(std::move(other.m_allocator))
        {
            MoveFrom(std::move(other));
        }

        /// @brief Constructs a value by storing a forwarded object.
        /// @tparam T Stored value type after removal of cv-ref qualifiers.
        /// @param value Object used to construct the payload.
        template<typename T>
            requires(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, Any>)
        explicit Any(T&& value)
        {
            Emplace<std::remove_cv_t<std::remove_reference_t<T>>>(std::forward<T>(value));
        }

        /// @brief Constructs a payload of type `T` in place.
        /// @tparam T Payload type.
        /// @tparam Args Constructor argument types.
        /// @param args Arguments forwarded to `T`'s constructor.
        template<typename T, typename... Args>
            requires(!std::is_same_v<std::remove_cv_t<T>, Any>)
        explicit Any(std::in_place_type_t<T>, Args&&... args)
        {
            Emplace<T>(std::forward<Args>(args)...);
        }

        /// @brief Destroys the stored payload, if any.
        ~Any()
        {
            Reset();
        }

        /// @brief Replaces this value with a copy of `other`.
        /// @param other Value to copy.
        /// @return This container.
        Any& operator=(const Any& other)
        {
            if (this == &other)
                return *this;
            Any copy(other);
            *this = std::move(copy);
            return *this;
        }

        /// @brief Replaces this value by moving from `other`, leaving `other` empty.
        /// @param other Value to consume.
        /// @return This container.
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

        /// @brief Replaces the payload with a forwarded object.
        /// @tparam T Stored value type after removal of cv-ref qualifiers.
        /// @param value Object used to construct the replacement payload.
        /// @return This container.
        template<typename T>
            requires(!std::is_same_v<std::remove_cv_t<std::remove_reference_t<T>>, Any>)
        Any& operator=(T&& value)
        {
            Emplace<std::remove_cv_t<std::remove_reference_t<T>>>(std::forward<T>(value));
            return *this;
        }

        /// @brief Replaces the payload with a value of type `T` constructed in place.
        /// @tparam T Payload type.
        /// @tparam Args Constructor argument types.
        /// @param args Arguments forwarded to `T`'s constructor.
        /// @return Reference to the newly stored value.
        /// @throws std::bad_alloc If out-of-line storage cannot be allocated.
        template<typename T, typename... Args>
            requires(!std::is_same_v<std::remove_cv_t<T>, Any>)
        T& Emplace(Args&&... args)
        {
            using Stored = std::remove_cv_t<T>;
            constexpr auto& descriptor =
                    detail::AnyDescriptorProvider<Stored, SboSize, Allocator, TypeIdPolicy>::descriptor;
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

        /// @brief Destroys the stored payload and makes the container empty.
        void Reset() noexcept
        {
            if (m_descriptor != nullptr)
            {
                m_descriptor->destroy(m_storage, m_allocator);
                m_descriptor      = nullptr;
                m_storage.heapPtr = nullptr;
            }
        }

        /// @brief Returns whether the container holds a payload.
        [[nodiscard]] bool HasValue() const noexcept
        {
            return m_descriptor != nullptr;
        }

        /// @brief Returns whether the current payload resides in inline storage.
        /// @return `false` when the container is empty or uses allocated storage.
        [[nodiscard]] bool IsInline() const noexcept
        {
            return m_descriptor != nullptr && m_descriptor->storesInline;
        }

        /// @brief Returns the current payload type identifier.
        /// @return `VOID_TYPE_ID` when the container is empty.
        [[nodiscard]] TypeId GetTypeId() const noexcept
        {
            return m_descriptor ? m_descriptor->typeId : VOID_TYPE_ID;
        }

        /// @brief Returns the payload size in bytes, or zero when empty.
        [[nodiscard]] UIntSize Size() const noexcept
        {
            return m_descriptor ? m_descriptor->sizeBytes : 0u;
        }

        /// @brief Returns the payload alignment requirement.
        /// @return `alignof(std::max_align_t)` when the container is empty.
        [[nodiscard]] UIntSize Alignment() const noexcept
        {
            return m_descriptor ? m_descriptor->alignment : alignof(std::max_align_t);
        }

        /// @brief Returns whether the stored payload has type `T`.
        /// @tparam T Type to compare after removal of cv-ref qualifiers.
        template<typename T>
        [[nodiscard]] bool Is() const noexcept
        {
            if (!m_descriptor)
                return false;
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            return m_descriptor->typeId == detail::AnyTypeIdOf<TypeIdPolicy, Base>();
        }

        /// @brief Returns mutable access to the payload when it has type `T`.
        /// @tparam T Requested payload type.
        /// @return Pointer to the payload, or `nullptr` on a type mismatch or empty container.
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

        /// @brief Returns immutable access to the payload when it has type `T`.
        /// @tparam T Requested payload type.
        /// @return Pointer to the payload, or `nullptr` on a type mismatch or empty container.
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

        /// @brief Returns mutable access to the payload as `T`.
        /// @tparam T Requested payload type.
        /// @throws std::bad_any_cast If the container is empty or contains another type.
        template<typename T>
        T& Cast()
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }

        /// @brief Returns immutable access to the payload as `T`.
        /// @tparam T Requested payload type.
        /// @throws std::bad_any_cast If the container is empty or contains another type.
        template<typename T>
        const T& Cast() const
        {
            auto* ptr = TryCast<T>();
            if (ptr == nullptr)
                throw std::bad_any_cast();
            return *ptr;
        }

        /// @brief Invokes a callable with a mutable view of the payload.
        /// @tparam Fn Callable type accepting `View`.
        /// @param fn Callable to invoke.
        /// @return The callable's result.
        /// @throws std::logic_error If the container is empty.
        template<typename Fn>
        decltype(auto) Visit(Fn&& fn)
        {
            if (!m_descriptor)
                throw std::logic_error("NGIN::Utilities::Any::Visit requires a value");
            View view {m_descriptor->access(m_storage), m_descriptor};
            return std::invoke(std::forward<Fn>(fn), view);
        }

        /// @brief Invokes a callable with an immutable view of the payload.
        /// @tparam Fn Callable type accepting `ConstView`.
        /// @param fn Callable to invoke.
        /// @return The callable's result.
        /// @throws std::logic_error If the container is empty.
        template<typename Fn>
        decltype(auto) Visit(Fn&& fn) const
        {
            if (!m_descriptor)
                throw std::logic_error("NGIN::Utilities::Any::Visit requires a value");
            ConstView view {m_descriptor->accessConst(m_storage), m_descriptor};
            return std::invoke(std::forward<Fn>(fn), view);
        }

        /// @brief Invokes a callable with a mutable view when a payload is present.
        /// @tparam Fn Callable type accepting `View`; its result is discarded.
        /// @param fn Callable to invoke.
        /// @return `true` when the callable was invoked.
        template<typename Fn>
        bool TryVisit(Fn&& fn)
        {
            if (!m_descriptor)
                return false;
            View view {m_descriptor->access(m_storage), m_descriptor};
            (void) std::invoke(std::forward<Fn>(fn), view);
            return true;
        }

        /// @brief Invokes a callable with an immutable view when a payload is present.
        /// @tparam Fn Callable type accepting `ConstView`; its result is discarded.
        /// @param fn Callable to invoke.
        /// @return `true` when the callable was invoked.
        template<typename Fn>
        bool TryVisit(Fn&& fn) const
        {
            if (!m_descriptor)
                return false;
            ConstView view {m_descriptor->accessConst(m_storage), m_descriptor};
            (void) std::invoke(std::forward<Fn>(fn), view);
            return true;
        }

        /// @brief Returns a mutable non-owning view of the payload.
        /// @warning The view is invalidated when this container is modified or destroyed.
        [[nodiscard]] View MakeView() noexcept
        {
            return View {m_descriptor ? m_descriptor->access(m_storage) : nullptr, m_descriptor};
        }

        /// @brief Returns an immutable non-owning view of the payload.
        /// @warning The view is invalidated when this container is modified or destroyed.
        [[nodiscard]] ConstView MakeView() const noexcept
        {
            return ConstView {m_descriptor ? m_descriptor->accessConst(m_storage) : nullptr, m_descriptor};
        }

        /// @brief Returns the allocator used for out-of-line payload storage.
        [[nodiscard]] Allocator& GetAllocator() noexcept
        {
            return m_allocator;
        }

        /// @brief Returns the allocator used for out-of-line payload storage.
        [[nodiscard]] const Allocator& GetAllocator() const noexcept
        {
            return m_allocator;
        }

        /// @brief Returns a mutable pointer to the stored object, or `nullptr` when empty.
        [[nodiscard]] void* Data() noexcept
        {
            if (m_descriptor == nullptr)
                return nullptr;
            return m_descriptor->access(m_storage);
        }

        /// @brief Returns an immutable pointer to the stored object, or `nullptr` when empty.
        [[nodiscard]] const void* Data() const noexcept
        {
            if (m_descriptor == nullptr)
                return nullptr;
            return m_descriptor->accessConst(m_storage);
        }

        /// @brief Creates a mutable non-owning view of an external object.
        /// @tparam T Referenced object type.
        /// @param value Object to reference.
        /// @return View that remains valid while `value` remains alive at the same address.
        template<typename T>
        [[nodiscard]] static View FromRef(T& value) noexcept
        {
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            constexpr auto& descriptor =
                    detail::AnyDescriptorProvider<Base, SboSize, Allocator, TypeIdPolicy>::descriptor;
            return View {static_cast<void*>(std::addressof(value)), &descriptor};
        }

        /// @brief Creates an immutable non-owning view of an external object.
        /// @tparam T Referenced object type.
        /// @param value Object to reference.
        /// @return View that remains valid while `value` remains alive at the same address.
        template<typename T>
        [[nodiscard]] static ConstView FromConstRef(const T& value) noexcept
        {
            using Base = std::remove_cv_t<std::remove_reference_t<T>>;
            constexpr auto& descriptor =
                    detail::AnyDescriptorProvider<Base, SboSize, Allocator, TypeIdPolicy>::descriptor;
            return ConstView {static_cast<const void*>(std::addressof(value)), &descriptor};
        }

        /// @brief Creates an empty `Any` value.
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
