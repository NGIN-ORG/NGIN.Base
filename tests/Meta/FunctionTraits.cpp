#include <NGIN/Meta/FunctionTraits.hpp>
#include <boost/ut.hpp>
#include <functional>
#include <string>

using namespace boost::ut;

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
        int operator()(std::string)
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

suite<"FunctionTraits"> _FunctionTraits = [] {
    "TestFunctionReturnTypeAndArgs"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestFunction), 0>, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestFunction), 1>, double>);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "MemberFunctionReturnTypeAndArgs"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestClass::memberFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestClass::memberFunction), 0>, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestClass::memberFunction), 1>, double>);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "ConstMemberFunctionReturnTypeAndArgs"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&TestClass::constMemberFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestClass::constMemberFunction), 0>, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&TestClass::constMemberFunction), 1>, double>);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == true);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "CallableObjectReturnTypeAndArgs"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<TestClass>;
        expect(std::is_same_v<typename Traits::ReturnType, int>);
        expect(std::is_same_v<ArgNTypeOf<TestClass, 0>, std::string>);

        // Check flags
        expect(Traits::is_member_function == true);// operator() is a member function
        expect(Traits::is_const == false);         // operator() is non-const
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "NoArgFunctionReturnType"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&noArgFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(Traits::NUM_ARGS == 0_u);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "MultiArgFunctionDifferentTypes"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&MultiTypeFunction)>;
        expect(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 0>, CustomType>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 1>, const int&>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&MultiTypeFunction), 2>, double*>);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "FunctionNonvoidReturnType"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&NonVoidReturnFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&NonVoidReturnFunction), 0>, int>);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "FunctionWithRvalueReferenceArgs"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&RvalueRefFunction)>;
        expect(std::is_same_v<ArgNTypeOf<decltype(&RvalueRefFunction), 0>, std::string&&>);
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(not std::is_same_v<ArgNTypeOf<decltype(&RvalueRefFunction), 0>, std::string>) << "Rvalue reference should not decay to an lvalue reference";

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "LambdaWithoutCaptures"_test = [] {
        auto lambda  = [](int a, double b) -> std::string { return std::to_string(a + b); };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        expect(std::is_same_v<typename Traits::ReturnType, std::string>);
        expect(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(lambda), 1>, double>);

        // Check flags
        expect(Traits::is_member_function == true);// operator() is a member function of the lambda closure type.
        expect(Traits::is_const == true);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);// Lambdas without noexcept are potentially throwing
        expect(Traits::is_variadic == false);
    };

    "LambdaWithCaptures"_test = [] {
        int x        = 42;
        auto lambda  = [x](std::string s) { return s + std::to_string(x); };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        expect(std::is_same_v<typename Traits::ReturnType, std::string>);
        expect(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, std::string>);

        // Check flags
        expect(Traits::is_member_function == true);// Capturing lambdas are callable objects with operator()
        expect(Traits::is_const == true);          // operator() is const by default
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "MutableLambda"_test = [] {
        int x        = 0;
        auto lambda  = [x]() mutable -> int { return ++x; };
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        expect(std::is_same_v<typename Traits::ReturnType, int>);
        expect(Traits::NUM_ARGS == 0_u);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == false);// Mutable lambdas have non-const operator()
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "GenericLambda"_test = [] {
        auto lambda = [](auto a, auto b) { return a + b; };
        // For generic lambdas, the `operator()` is a template and cannot be directly used.
        // We need to specify the types explicitly.
        using FunctionPtr   = int (*)(int, int);
        FunctionPtr funcPtr = +[](int a, int b) { return a + b; };
        using Traits        = NGIN::Meta::FunctionTraits<decltype(funcPtr)>;
        expect(std::is_same_v<typename Traits::ReturnType, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(funcPtr), 0>, int>);
        expect(std::is_same_v<ArgNTypeOf<decltype(funcPtr), 1>, int>);

        // Check flags
        expect(Traits::is_member_function == false);// Function pointer
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == false);
    };

    "LambdaWithNoexcept"_test = [] {
        auto lambda  = [](int a) noexcept -> void {};
        using Traits = NGIN::Meta::FunctionTraits<decltype(lambda)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(lambda), 0>, int>);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == true);   // operator() is const by default
        expect(Traits::is_noexcept == true);// noexcept lambda
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_variadic == false);
    };

    "LambdaWithRefQualifiers"_test = [] {
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

        // Define the function pointer type for the lvalue ref-qualified operator()
        using LvalueOperatorType = const char* (LambdaHolder::*) ()&;
        using TraitsLvalue       = NGIN::Meta::FunctionTraits<LvalueOperatorType>;

        expect(std::is_same_v<typename TraitsLvalue::ReturnType, const char*>);
        expect(TraitsLvalue::NUM_ARGS == 0_u);

        // Check flags
        expect(TraitsLvalue::is_member_function == true);
        expect(TraitsLvalue::is_const == false);
        expect(TraitsLvalue::is_lvalue_ref == true);
        expect(TraitsLvalue::is_rvalue_ref == false);
        expect(TraitsLvalue::is_noexcept == false);
        expect(TraitsLvalue::is_volatile == false);
        expect(TraitsLvalue::is_variadic == false);

        // Similarly, define the function pointer type for the rvalue ref-qualified operator()
        using RvalueOperatorType = const char* (LambdaHolder::*) ()&&;
        using TraitsRvalue       = NGIN::Meta::FunctionTraits<RvalueOperatorType>;

        expect(std::is_same_v<typename TraitsRvalue::ReturnType, const char*>);
        expect(TraitsRvalue::NUM_ARGS == 0_u);

        // Check flags
        expect(TraitsRvalue::is_member_function == true);
        expect(TraitsRvalue::is_const == false);
        expect(TraitsRvalue::is_lvalue_ref == false);
        expect(TraitsRvalue::is_rvalue_ref == true);
        expect(TraitsRvalue::is_noexcept == false);
        expect(TraitsRvalue::is_volatile == false);
        expect(TraitsRvalue::is_variadic == false);
    };

    "LambdaWithConstVolatile"_test = [] {
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
            expect(std::is_same_v<typename TraitsInt::ReturnType, int>);
            expect(std::is_same_v<typename TraitsInt::template ArgNType<0>, int>);
            expect(TraitsInt::NUM_ARGS == 1_u);

            // Check flags
            expect(TraitsInt::is_member_function == true);
            expect(TraitsInt::is_const == true);
            expect(TraitsInt::is_volatile == false);
            expect(TraitsInt::is_lvalue_ref == false);
            expect(TraitsInt::is_rvalue_ref == false);
            expect(TraitsInt::is_noexcept == false);
            expect(TraitsInt::is_variadic == false);
        }

        // Testing the volatile double overload
        {
            using FunctionPtrDouble = double (LambdaHolder::*)(double) volatile;
            using TraitsDouble      = NGIN::Meta::FunctionTraits<FunctionPtrDouble>;
            expect(std::is_same_v<typename TraitsDouble::ReturnType, double>);
            expect(std::is_same_v<typename TraitsDouble::template ArgNType<0>, double>);
            expect(TraitsDouble::NUM_ARGS == 1_u);

            // Check flags
            expect(TraitsDouble::is_member_function == true);
            expect(TraitsDouble::is_const == false);
            expect(TraitsDouble::is_volatile == true);
            expect(TraitsDouble::is_lvalue_ref == false);
            expect(TraitsDouble::is_rvalue_ref == false);
            expect(TraitsDouble::is_noexcept == false);
            expect(TraitsDouble::is_variadic == false);
        }

        // Testing the const volatile std::string overload
        {
            using FunctionPtrString = std::string (LambdaHolder::*)(std::string) const volatile;
            using TraitsString      = NGIN::Meta::FunctionTraits<FunctionPtrString>;
            expect(std::is_same_v<typename TraitsString::ReturnType, std::string>);
            expect(std::is_same_v<typename TraitsString::template ArgNType<0>, std::string>);
            expect(TraitsString::NUM_ARGS == 1_u);

            // Check flags
            expect(TraitsString::is_member_function == true);
            expect(TraitsString::is_const == true);
            expect(TraitsString::is_volatile == true);
            expect(TraitsString::is_lvalue_ref == false);
            expect(TraitsString::is_rvalue_ref == false);
            expect(TraitsString::is_noexcept == false);
            expect(TraitsString::is_variadic == false);
        }
    };

    "NoexceptFunction"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&NoexceptFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&NoexceptFunction), 0>, int>);
        expect(Traits::NUM_ARGS == 1_u);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == true);// noexcept function
        expect(Traits::is_variadic == false);
    };

    "VariadicFunction"_test = [] {
        using Traits = NGIN::Meta::FunctionTraits<decltype(&VariadicFunction)>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<ArgNTypeOf<decltype(&VariadicFunction), 0>, int>);
        expect(Traits::NUM_ARGS == 1_u);

        // Check flags
        expect(Traits::is_member_function == false);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == true);// variadic function
    };

    "VariadicMemberFunction"_test = [] {
        struct VariadicClass
        {
            void variadicMemberFunction(int, ...) {}
        };
        using FunctionPtr = void (VariadicClass::*)(int, ...);
        using Traits      = NGIN::Meta::FunctionTraits<FunctionPtr>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<typename Traits::ClassType, VariadicClass>);
        expect(std::is_same_v<typename Traits::template ArgNType<0>, int>);
        expect(Traits::NUM_ARGS == 1_u);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == false);
        expect(Traits::is_variadic == true);
    };

    "NoexceptMemberFunction"_test = [] {
        struct NoexceptClass
        {
            void noexceptMemberFunction(int) noexcept {}
        };
        using FunctionPtr = void (NoexceptClass::*)(int) noexcept;
        using Traits      = NGIN::Meta::FunctionTraits<FunctionPtr>;
        expect(std::is_same_v<typename Traits::ReturnType, void>);
        expect(std::is_same_v<typename Traits::ClassType, NoexceptClass>);
        expect(std::is_same_v<typename Traits::template ArgNType<0>, int>);
        expect(Traits::NUM_ARGS == 1_u);

        // Check flags
        expect(Traits::is_member_function == true);
        expect(Traits::is_const == false);
        expect(Traits::is_volatile == false);
        expect(Traits::is_lvalue_ref == false);
        expect(Traits::is_rvalue_ref == false);
        expect(Traits::is_noexcept == true);// noexcept member function
        expect(Traits::is_variadic == false);
    };
};
