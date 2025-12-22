#pragma once
#include <NGIN/Defines.hpp>
#include <functional>
#include <tuple>
#include <type_traits>
namespace NGIN
{
    namespace Meta
    {
        // Helper trait to check if a type has an operator()
        template<typename T>
        struct IsCallable
        {
        private:
            template<typename U>
            static auto Test(U*) -> decltype(&U::operator(), std::true_type {});

            template<typename>
            static auto Test(...) -> std::false_type
            {
                return std::false_type {};
            }

        public:
            static constexpr bool value = decltype(Test<T>(nullptr))::value;
        };

        // Base template for function traits
        template<typename R, typename... Args>
        struct FunctionTraitsBase
        {
            using ReturnType                      = R;
            using ArgsTupleType                   = std::tuple<Args...>;
            static constexpr std::size_t NUM_ARGS = sizeof...(Args);

            template<std::size_t N>
            using ArgNType = std::tuple_element_t<N, ArgsTupleType>;

            // Default flags
            static constexpr bool is_member_function = false;
            static constexpr bool is_const           = false;
            static constexpr bool is_volatile        = false;
            static constexpr bool is_lvalue_ref      = false;
            static constexpr bool is_rvalue_ref      = false;
            static constexpr bool is_noexcept        = false;
            static constexpr bool is_variadic        = false;
        };

        // Base template
        template<typename T, bool IsCallableV = IsCallable<T>::value>
        struct FunctionTraits;

        // Specialization for function pointers
        template<typename R, typename... Args>
        struct FunctionTraits<R (*)(Args...), false> : FunctionTraitsBase<R, Args...>
        {
        };

        // Specialization for variadic function pointers
        template<typename R, typename... Args>
        struct FunctionTraits<R (*)(Args..., ...), false> : FunctionTraitsBase<R, Args...>
        {
            static constexpr bool is_variadic = true;
        };

        // Function reference
        template<typename R, typename... Args>
        struct FunctionTraits<R (&)(Args...), false> : FunctionTraits<R (*)(Args...)>
        {
        };

        // Noexcept function pointer
        template<typename R, typename... Args>
        struct FunctionTraits<R (*)(Args...) noexcept, false> : FunctionTraitsBase<R, Args...>
        {
            static constexpr bool is_noexcept = true;
        };

        // Member function pointer traits
        template<typename C, typename R, typename... Args>
        struct FunctionTraitsBaseMember : FunctionTraitsBase<R, Args...>
        {
            using ClassType                          = C;
            static constexpr bool is_member_function = true;
        };

        // Specialization for non-const, non-volatile member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...), false> : FunctionTraitsBaseMember<C, R, Args...>
        {
        };

        // Const member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) const, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_const = true;
        };

        // Volatile member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) volatile, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_volatile = true;
        };

        // Const volatile member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) const volatile, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_const    = true;
            static constexpr bool is_volatile = true;
        };

        // Lvalue ref-qualified member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) &, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_lvalue_ref = true;
        };

        // Const lvalue ref-qualified member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) const&, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_const      = true;
            static constexpr bool is_lvalue_ref = true;
        };

        // Rvalue ref-qualified member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) &&, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_rvalue_ref = true;
        };

        // Const rvalue ref-qualified member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) const&&, false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_const      = true;
            static constexpr bool is_rvalue_ref = true;
        };

        // Noexcept member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) noexcept, false> : FunctionTraits<R (C::*)(Args...), false>
        {
            static constexpr bool is_noexcept = true;
        };

        // Const noexcept member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) const noexcept, false> : FunctionTraits<R (C::*)(Args...) const, false>
        {
            static constexpr bool is_noexcept = true;
        };

        // Lvalue ref-qualified noexcept member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) & noexcept, false> : FunctionTraits<R (C::*)(Args...) &, false>
        {
            static constexpr bool is_noexcept = true;
        };

        // Rvalue ref-qualified noexcept member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args...) && noexcept, false> : FunctionTraits<R (C::*)(Args...) &&, false>
        {
            static constexpr bool is_noexcept = true;
        };

        // Variadic member function
        template<typename C, typename R, typename... Args>
        struct FunctionTraits<R (C::*)(Args..., ...), false> : FunctionTraitsBaseMember<C, R, Args...>
        {
            static constexpr bool is_variadic = true;
        };

        // Specialization for std::function
        template<typename R, typename... Args>
        struct FunctionTraits<std::function<R(Args...)>, false> : FunctionTraitsBase<R, Args...>
        {
        };

        // Specialization for callable objects
        template<typename T>
        struct FunctionTraits<T, true> : FunctionTraits<decltype(&T::operator())>
        {
        };

        // Fallback for unsupported types
        template<typename T>
        struct FunctionTraits<T, false>
        {
            static_assert(!std::is_same_v<T, T>, "FunctionTraits cannot handle this type.");
        };
    }// namespace Meta
}// namespace NGIN
