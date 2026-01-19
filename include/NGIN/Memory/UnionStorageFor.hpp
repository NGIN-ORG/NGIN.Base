#pragma once

#include <cstddef>      // std::byte, std::size_t
#include <new>          // ::new, std::launder
#include <type_traits>  // std::is_*
#include <utility>      // std::forward

namespace NGIN::Memory
{
    namespace detail
    {
        template <std::size_t A, std::size_t B>
        inline constexpr std::size_t MaxSize = (A > B) ? A : B;

        template <std::size_t A, std::size_t B>
        inline constexpr std::size_t MaxAlign = (A > B) ? A : B;

        template <class... Ts>
        struct UnionStorageLayout;

        template <class T>
        struct UnionStorageLayout<T>
        {
            static inline constexpr std::size_t Size = sizeof(T);
            static inline constexpr std::size_t Align = alignof(T);
        };

        template <class T, class... Ts>
        struct UnionStorageLayout<T, Ts...>
        {
            static inline constexpr std::size_t Size = MaxSize<sizeof(T), UnionStorageLayout<Ts...>::Size>;
            static inline constexpr std::size_t Align = MaxAlign<alignof(T), UnionStorageLayout<Ts...>::Align>;
        };

        template <class U, class... Ts>
        inline constexpr bool UnionStorageContains = (std::is_same_v<U, Ts> || ...);

        template <class... Ts>
        class UnionStorageForCommon
        {
            static_assert(sizeof...(Ts) > 0, "UnionStorageFor<Ts...> requires at least one type.");
            static_assert((!std::is_reference_v<Ts> && ...), "UnionStorageFor<T&> is not supported.");
            static_assert((!std::is_void_v<Ts> && ...), "UnionStorageFor<void> is not supported.");

        public:
            constexpr UnionStorageForCommon() noexcept = default;
            ~UnionStorageForCommon() = default;

            template <class U>
            constexpr U* Ptr() noexcept
            {
                using Self = std::remove_cvref_t<U>;
                static_assert(detail::UnionStorageContains<Self, Ts...>, "Type not supported by this UnionStorageFor.");
                return std::launder(reinterpret_cast<Self*>(m_data));
            }

            template <class U>
            constexpr const U* Ptr() const noexcept
            {
                using Self = std::remove_cvref_t<U>;
                static_assert(detail::UnionStorageContains<Self, Ts...>, "Type not supported by this UnionStorageFor.");
                return std::launder(reinterpret_cast<const Self*>(m_data));
            }

            template <class U>
            constexpr U& Ref() noexcept
            {
                return *Ptr<U>();
            }

            template <class U>
            constexpr const U& Ref() const noexcept
            {
                return *Ptr<U>();
            }

            template <class U, class... Args>
            constexpr U& Construct(Args&&... args) noexcept(std::is_nothrow_constructible_v<std::remove_cvref_t<U>, Args...>)
            {
                using Self = std::remove_cvref_t<U>;
                static_assert(detail::UnionStorageContains<Self, Ts...>, "Type not supported by this UnionStorageFor.");
                ::new (static_cast<void*>(m_data)) Self(std::forward<Args>(args)...);
                return Ref<Self>();
            }

            template <class U>
            constexpr void Destroy() noexcept
            {
                using Self = std::remove_cvref_t<U>;
                static_assert(detail::UnionStorageContains<Self, Ts...>, "Type not supported by this UnionStorageFor.");

                if constexpr (!std::is_trivially_destructible_v<Self>)
                {
                    Ref<Self>().~Self();
                }
            }

            static constexpr std::size_t Size() noexcept { return detail::UnionStorageLayout<Ts...>::Size; }
            static constexpr std::size_t Alignment() noexcept { return detail::UnionStorageLayout<Ts...>::Align; }

        protected:
            alignas(detail::UnionStorageLayout<Ts...>::Align) std::byte m_data[detail::UnionStorageLayout<Ts...>::Size] {};
        };
    }

    namespace detail
    {
        template <bool IsTriviallyCopyable, class... Ts>
        class UnionStorageForImpl;

        /// @brief Trivially-copyable union storage when all `Ts...` are trivially copyable.
        template <class... Ts>
        class UnionStorageForImpl<true, Ts...> : public UnionStorageForCommon<Ts...>
        {
        public:
            constexpr UnionStorageForImpl() noexcept = default;
            UnionStorageForImpl(const UnionStorageForImpl&) noexcept = default;
            UnionStorageForImpl(UnionStorageForImpl&&) noexcept = default;
            UnionStorageForImpl& operator=(const UnionStorageForImpl&) noexcept = default;
            UnionStorageForImpl& operator=(UnionStorageForImpl&&) noexcept = default;
            ~UnionStorageForImpl() = default;
        };

        /// @brief Non-copyable/non-movable union storage when any `Ts...` is not trivially copyable.
        template <class... Ts>
        class UnionStorageForImpl<false, Ts...> : public UnionStorageForCommon<Ts...>
        {
        public:
            constexpr UnionStorageForImpl() noexcept = default;

            UnionStorageForImpl(const UnionStorageForImpl&) = delete;
            UnionStorageForImpl(UnionStorageForImpl&&) = delete;
            UnionStorageForImpl& operator=(const UnionStorageForImpl&) = delete;
            UnionStorageForImpl& operator=(UnionStorageForImpl&&) = delete;

            ~UnionStorageForImpl() = default;
        };
    }

    template <class... Ts>
    using UnionStorageFor = detail::UnionStorageForImpl<(std::is_trivially_copyable_v<Ts> && ...), Ts...>;
} // namespace NGIN::Memory
