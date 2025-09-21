#include <NGIN/Meta/FunctionTraits.hpp>
#include <catch2/catch_test_macros.hpp>
#include <functional>
#include <string>


namespace
{
    // A set of test functions and objects to use with FunctionTraits

    // Simple test function with two arguments
    void TestFunction(int, double) {}

    // A test class with member functions and a callable operator
    struct TestClass
    {
        void memberFunction(int, double) {}
        void constMemberFunction(int, double) const {}
        int  operator()(std::string)
        {
            return 0;
        }
    };

    // No-argument function
    void noArgFunction() {}

    // Custom type for testing
    struct CustomType
    {
    };

    // Function with multiple argument types
    void MultiTypeFunction(CustomType, const int&, double*) {}

    // Function with non-void return type
    int NonVoidReturnFunction(int)
    {
        return 0;
    }

    // Function with rvalue reference argument
    void RvalueRefFunction(std::string&&) {}

    // Noexcept function
    void NoexceptFunction(int) noexcept {}

    // Variadic function
    void VariadicFunction(int, ...) {}

    // Helper type alias to simplify the tests
    template<typename F, std::size_t N>
    using ArgNTypeOf = typename NGIN::Meta::FunctionTraits<F>::template ArgNType<N>;
}// namespace

TEST_CASE("FunctionTraits", "[Meta][FunctionTraits]")
{
    SECTION("TestFunctionReturnTypeAndArgs")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestFunction), 0>, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestFunction), 1>, double>);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("MemberFunctionReturnTypeAndArgs")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestClass::memberFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestClass::memberFunction), 0>, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestClass::memberFunction), 1>, double>);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("ConstMemberFunctionReturnTypeAndArgs")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestClass::constMemberFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestClass::constMemberFunction), 0>, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&TestClass::constMemberFunction), 1>, double>);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("CallableObjectReturnTypeAndArgs")
    {
        using Traits = NGIN::Meta::FunctionTraits<TestClass>;
        CHECK(std::is_same_v<typename Traits::ReturnType, int>);
        CHECK(std::is_same_v<ArgNTypeOf<TestClass, 0>, std::string>);

        // Check flags
        CHECK(Traits::is_member_function);// operator() is a member function
        CHECK_FALSE(Traits::is_const);    // operator() is non-const
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("NoArgFunctionReturnType")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&noArgFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(Traits::NUM_ARGS == 0U);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("MultiArgFunctionDifferentTypes")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&MultiTypeFunction)>;
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 0>, CustomType>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 1>, const int&>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 2>, double*>);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("FunctionNonvoidReturnType")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&NonVoidReturnFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&NonVoidReturnFunction), 0>, int>);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("FunctionWithRvalueReferenceArgs")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&RvalueRefFunction)>;
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&RvalueRefFunction), 0>, std::string&&>);
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        INFO("Rvalue reference should not decay to an lvalue reference");
        CHECK_FALSE(std::is_same_v<ArgNTypeOf<decltype(&RvalueRefFunction), 0>, std::string>);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("LambdaWithoutCaptures")
    {
        auto lambda  = [](int a, double b) -> std::string { return std::to_string(a + b); };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, std::string>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(lambda), 1>, double>);

        // Check flags
        CHECK(Traits::is_member_function);// operator() is a member function of the lambda closure type.
        CHECK(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);// Lambdas without noexcept are potentially throwing
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("LambdaWithCaptures")
    {
        int  x       = 42;
        auto lambda  = [x](std::string s) { return s + std::to_string(x); };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, std::string>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, std::string>);

        // Check flags
        CHECK(Traits::is_member_function);// Capturing lambdas are callable objects with operator()
        CHECK(Traits::is_const);          // operator() is const by default
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("MutableLambda")
    {
        int  x       = 0;
        auto lambda  = [x]() mutable -> int { return ++x; };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, int>);
        CHECK(Traits::NUM_ARGS == 0U);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);// Mutable lambdas have non-const operator()
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("GenericLambda")
    {
        auto lambda = [](auto a, auto b) { return a + b; };
        static_cast<void>(lambda);
        // For generic lambdas, the `operator()` is a template and cannot be directly used.
        // We need to specify the types explicitly.
        using FunctionPtr   = int (*)(int, int);
        FunctionPtr funcPtr = +[](int a, int b) { return a + b; };
        using Traits        = NGIN::Meta::FunctionTraits<decltype(funcPtr)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(funcPtr), 0>, int>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(funcPtr), 1>, int>);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);// Function pointer
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("LambdaWithNoexcept")
    {
        auto lambda  = [](int /*unused*/) noexcept -> void {};
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, int>);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK(Traits::is_const);   // operator() is const by default
        CHECK(Traits::is_noexcept);// noexcept lambda
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("LambdaWithRefQualifiers")
    {
        struct LambdaHolder
        {
            auto operator()() &
            {
                return "lvalue";
            }
            auto operator()() &&
            {
                return "rvalue";
            }
        };
        LambdaHolder lambda;
        static_cast<void>(lambda);

        // Define the function pointer type for the lvalue ref-qualified operator()
        using LvalueOperatorType = const char* (LambdaHolder::*) ()&;
        using TraitsLvalue       = NGIN::Meta::FunctionTraits<LvalueOperatorType>;

        CHECK(std::is_same_v<typename TraitsLvalue::ReturnType, const char*>);
        CHECK(TraitsLvalue::NUM_ARGS == 0U);

        // Check flags
        CHECK(TraitsLvalue::is_member_function);
        CHECK_FALSE(TraitsLvalue::is_const);
        CHECK(TraitsLvalue::is_lvalue_ref);
        CHECK_FALSE(TraitsLvalue::is_rvalue_ref);
        CHECK_FALSE(TraitsLvalue::is_noexcept);
        CHECK_FALSE(TraitsLvalue::is_volatile);
        CHECK_FALSE(TraitsLvalue::is_variadic);

        // Similarly, define the function pointer type for the rvalue ref-qualified operator()
        using RvalueOperatorType = const char* (LambdaHolder::*) ()&&;
        using TraitsRvalue       = NGIN::Meta::FunctionTraits<RvalueOperatorType>;

        CHECK(std::is_same_v<typename TraitsRvalue::ReturnType, const char*>);
        CHECK(TraitsRvalue::NUM_ARGS == 0U);

        // Check flags
        CHECK(TraitsRvalue::is_member_function);
        CHECK_FALSE(TraitsRvalue::is_const);
        CHECK_FALSE(TraitsRvalue::is_lvalue_ref);
        CHECK(TraitsRvalue::is_rvalue_ref);
        CHECK_FALSE(TraitsRvalue::is_noexcept);
        CHECK_FALSE(TraitsRvalue::is_volatile);
        CHECK_FALSE(TraitsRvalue::is_variadic);
    }

    SECTION("LambdaWithConstVolatile")
    {
        struct LambdaHolder
        {
            auto operator()(int) const
            {
                return 0;
            }
            auto operator()(double) volatile
            {
                return 0.0;
            }
            auto operator()(std::string) const volatile
            {
                return std::string {};
            }
        };

        // Testing the const int overload
        {
            using FunctionPtrInt = int (LambdaHolder::*)(int) const;
            using TraitsInt      = NGIN::Meta::FunctionTraits<FunctionPtrInt>;
            CHECK(std::is_same_v<typename TraitsInt::ReturnType, int>);
            CHECK(std::is_same_v<typename TraitsInt::template ArgNType<0>, int>);
            CHECK(TraitsInt::NUM_ARGS == 1U);

            // Check flags
            CHECK(TraitsInt::is_member_function);
            CHECK(TraitsInt::is_const);
            CHECK_FALSE(TraitsInt::is_volatile);
            CHECK_FALSE(TraitsInt::is_lvalue_ref);
            CHECK_FALSE(TraitsInt::is_rvalue_ref);
            CHECK_FALSE(TraitsInt::is_noexcept);
            CHECK_FALSE(TraitsInt::is_variadic);
        }

        // Testing the volatile double overload
        {
            using FunctionPtrDouble = double (LambdaHolder::*)(double) volatile;
            using TraitsDouble      = NGIN::Meta::FunctionTraits<FunctionPtrDouble>;
            CHECK(std::is_same_v<typename TraitsDouble::ReturnType, double>);
            CHECK(std::is_same_v<typename TraitsDouble::template ArgNType<0>, double>);
            CHECK(TraitsDouble::NUM_ARGS == 1U);

            // Check flags
            CHECK(TraitsDouble::is_member_function);
            CHECK_FALSE(TraitsDouble::is_const);
            CHECK(TraitsDouble::is_volatile);
            CHECK_FALSE(TraitsDouble::is_lvalue_ref);
            CHECK_FALSE(TraitsDouble::is_rvalue_ref);
            CHECK_FALSE(TraitsDouble::is_noexcept);
            CHECK_FALSE(TraitsDouble::is_variadic);
        }

        // Testing the const volatile std::string overload
        {
            using FunctionPtrString = std::string (LambdaHolder::*)(std::string) const volatile;
            using TraitsString      = NGIN::Meta::FunctionTraits<FunctionPtrString>;
            CHECK(std::is_same_v<typename TraitsString::ReturnType, std::string>);
            CHECK(std::is_same_v<typename TraitsString::template ArgNType<0>, std::string>);
            CHECK(TraitsString::NUM_ARGS == 1U);

            // Check flags
            CHECK(TraitsString::is_member_function);
            CHECK(TraitsString::is_const);
            CHECK(TraitsString::is_volatile);
            CHECK_FALSE(TraitsString::is_lvalue_ref);
            CHECK_FALSE(TraitsString::is_rvalue_ref);
            CHECK_FALSE(TraitsString::is_noexcept);
            CHECK_FALSE(TraitsString::is_variadic);
        }
    }

    SECTION("NoexceptFunction")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&NoexceptFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&NoexceptFunction), 0>, int>);
        CHECK(Traits::NUM_ARGS == 1U);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK(Traits::is_noexcept);// noexcept function
        CHECK_FALSE(Traits::is_variadic);
    }

    SECTION("VariadicFunction")
    {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&VariadicFunction)>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<ArgNTypeOf<decltype(&VariadicFunction), 0>, int>);
        CHECK(Traits::NUM_ARGS == 1U);

        // Check flags
        CHECK_FALSE(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK(Traits::is_variadic);// variadic function
    }

    SECTION("VariadicMemberFunction")
    {
        struct VariadicClass
        {
            void variadicMemberFunction(int, ...) {}
        };
        using FunctionPtr = void (VariadicClass::*)(int, ...);
        using Traits      = NGIN::Meta::FunctionTraits<FunctionPtr>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<typename Traits::ClassType, VariadicClass>);
        CHECK(std::is_same_v<typename Traits::template ArgNType<0>, int>);
        CHECK(Traits::NUM_ARGS == 1U);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK_FALSE(Traits::is_noexcept);
        CHECK(Traits::is_variadic);
    }

    SECTION("NoexceptMemberFunction")
    {
        struct NoexceptClass
        {
            void noexceptMemberFunction(int) noexcept {}
        };
        using FunctionPtr = void (NoexceptClass::*)(int) noexcept;
        using Traits      = NGIN::Meta::FunctionTraits<FunctionPtr>;
        CHECK(std::is_same_v<typename Traits::ReturnType, void>);
        CHECK(std::is_same_v<typename Traits::ClassType, NoexceptClass>);
        CHECK(std::is_same_v<typename Traits::template ArgNType<0>, int>);
        CHECK(Traits::NUM_ARGS == 1U);

        // Check flags
        CHECK(Traits::is_member_function);
        CHECK_FALSE(Traits::is_const);
        CHECK_FALSE(Traits::is_volatile);
        CHECK_FALSE(Traits::is_lvalue_ref);
        CHECK_FALSE(Traits::is_rvalue_ref);
        CHECK(Traits::is_noexcept);// noexcept member function
        CHECK_FALSE(Traits::is_variadic);
    }
}
