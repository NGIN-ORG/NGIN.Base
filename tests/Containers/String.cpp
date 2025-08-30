/// @file StringTest.cpp
/// @brief Tests for NGIN::Containers::String using boost::ut
///
/// This suite covers various construction, assignment, and append scenarios,
/// including edge cases and transitions from SBO to heap storage.

#include <NGIN/Containers/String.hpp>// Include your custom string header
#include <boost/ut.hpp>
#include <cstring>// For std::strcmp

using namespace boost::ut;

namespace
{
    /// @brief Helper function to compare two C-strings safely in tests.
    /// @param a First string
    /// @param b Second string
    /// @return True if a and b are identical textually, false otherwise.
    bool CStrEqual(const char* a, const char* b)
    {
        if (!a || !b)
        {
            // If either is null, they match only if both are null.
            return a == b;
        }
        return std::strcmp(a, b) == 0;
    }
}// end anonymous namespace

suite<"NGIN::Containers::String"> stringTests = [] {
    "DefaultConstructor"_test = [] {
        NGIN::Containers::String s;
        expect(s.GetSize() == 0_ul);
        expect(CStrEqual(s.CStr(), ""));
    };

    "ConstructFromNullPointer"_test = [] {
        // Should behave like an empty string
        const char*              nullStr = nullptr;
        NGIN::Containers::String s(nullStr);
        expect(s.GetSize() == 0_ul);
        expect(CStrEqual(s.CStr(), ""));
    };

    "ConstructFromCStr_SmallBuffer"_test = [] {
        // Fits into SBO
        const char*              text = "Hello";
        NGIN::Containers::String s(text);
        expect(s.GetSize() == 5_ul);
        expect(CStrEqual(s.CStr(), text));
    };

    "ConstructFromCStr_HeapAllocation"_test = [] {
        // Ensure we exceed the SBO threshold
        // sboSize in the library is 48 => we need more than that
        std::string              bigString(60, 'A');// 60 'A's
        NGIN::Containers::String s(bigString.c_str());
        expect(s.GetSize() == 60_ul);
        expect(CStrEqual(s.CStr(), bigString.c_str()));
    };

    "CopyConstructor_SmallBuffer"_test = [] {
        NGIN::Containers::String original("Small Test");
        NGIN::Containers::String copy(original);
        expect(copy.GetSize() == original.GetSize());
        expect(CStrEqual(copy.CStr(), original.CStr()));
    };

    "CopyConstructor_Heap"_test = [] {
        // Force a heap usage
        std::string              big(70, 'B');
        NGIN::Containers::String original(big.c_str());
        NGIN::Containers::String copy(original);
        expect(copy.GetSize() == original.GetSize());
        expect(CStrEqual(copy.CStr(), original.CStr()));

        // Ensure it's a deep copy
        expect(original.CStr() != copy.CStr())
                << "Should not share the same pointer after copy";
    };

    "MoveConstructor_SmallBuffer"_test = [] {
        NGIN::Containers::String source("MoveSmall");
        const char*              oldPointer = source.CStr();

        NGIN::Containers::String moved(std::move(source));
        expect(moved.GetSize() == 9_ul);
        expect(CStrEqual(moved.CStr(), "MoveSmall"));

        // The source might still have something in SBO,
        // but logically it's an unspecified state. We only test that it's valid.
        expect(oldPointer != moved.CStr())
                << "SBO move typically copies the buffer, so pointers differ";
    };

    "MoveConstructor_Heap"_test = [] {
        std::string              big(70, 'M');
        NGIN::Containers::String source(big.c_str());
        const char*              oldPointer = source.CStr();

        NGIN::Containers::String moved(std::move(source));
        expect(moved.GetSize() == 70_ul);
        expect(CStrEqual(moved.CStr(), big.c_str()));

        // In heap mode, we expect the pointer to have been moved
        expect(oldPointer == moved.CStr());
    };

    "CopyAssignmentOperator_SmallToSmall"_test = [] {
        NGIN::Containers::String s1("Alpha");
        NGIN::Containers::String s2("Beta");
        s2 = s1;
        expect(s2.GetSize() == s1.GetSize());
        expect(CStrEqual(s2.CStr(), "Alpha"));
    };

    "CopyAssignmentOperator_HeapToHeap"_test = [] {
        // Create two large strings
        std::string largeA(80, 'A');
        std::string largeB(90, 'B');

        NGIN::Containers::String s1(largeA.c_str());
        NGIN::Containers::String s2(largeB.c_str());
        s2 = s1;
        expect(s2.GetSize() == s1.GetSize());
        expect(CStrEqual(s2.CStr(), largeA.c_str()));
        // Ensure deep copy
        expect(s2.CStr() != s1.CStr());
    };

    "MoveAssignmentOperator_Small"_test = [] {
        NGIN::Containers::String s1("Hello");
        NGIN::Containers::String s2("World");
        const char*              oldPointer = s1.CStr();// SBO pointer

        s2 = std::move(s1);
        expect(s2.GetSize() == 5_ul);
        expect(CStrEqual(s2.CStr(), "Hello"));
        // For SBO, a move typically does a memcpy, so we don't necessarily see pointer reuse.
        expect(oldPointer != s2.CStr());
    };

    "MoveAssignmentOperator_Heap"_test = [] {
        std::string              big(75, 'Z');
        NGIN::Containers::String s1(big.c_str());
        NGIN::Containers::String s2("Small");
        const char*              oldPointer = s1.CStr();

        s2 = std::move(s1);
        expect(s2.GetSize() == 75_ul);
        expect(CStrEqual(s2.CStr(), big.c_str()));
        expect(s2.CStr() == oldPointer);// Moved pointer
    };

    "Append_SBOToSBO"_test = [] {
        // Both small enough to stay in SBO
        NGIN::Containers::String s1("Hello");
        NGIN::Containers::String s2("World");
        s1.Append(s2);

        expect(s1.GetSize() == 10_ul);
        expect(CStrEqual(s1.CStr(), "HelloWorld"));
        // Should still be in SBO if total length < 47
    };

    "Append_TriggersHeap"_test = [] {
        NGIN::Containers::String s1("SBO start: ");
        std::string              big(60, 'X');
        NGIN::Containers::String s2(big.c_str());
        std::cout << "S1 Size: " << s1.GetSize() << std::endl;

        s1.Append(s2);
        std::cout << "S1 Size: " << s1.GetSize() << std::endl;
        std::cout << "S2 Size: " << s2.GetSize() << std::endl;
        expect(s1.GetSize() == (11 + 60));// "SBO start: " is length 11

        std::string expected = "SBO start: " + big;
        std::cout << "Expected: " << expected << std::endl;
        std::cout << "Actual:   " << s1.CStr() << std::endl;
        expect(CStrEqual(s1.CStr(), expected.c_str()));
    };

    "OperatorPlusEquals_SmokeTest"_test = [] {
        NGIN::Containers::String s("Test");
        NGIN::Containers::String suffix("++");
        s += suffix;
        expect(s.GetSize() == 6_ul);
        expect(CStrEqual(s.CStr(), "Test++"));
    };

    "SelfAssignment_Operator="_test = [] {
        // Edge case: object = object
        NGIN::Containers::String s("Self");
        s = s;
        expect(s.GetSize() == 4_ul);
        expect(CStrEqual(s.CStr(), "Self"));
    };

    "SelfAssignment_Operator+="_test = [] {
        // By design, s += s is allowed but might be unusual.
        // Let's see if it appends a copy or does something weird.
        NGIN::Containers::String s("Self");
        s += s;
        // Expect "SelfSelf"
        expect(s.GetSize() == 8_ul);
        expect(CStrEqual(s.CStr(), "SelfSelf"));
    };

    "EmptyStrings_Appending"_test = [] {
        NGIN::Containers::String empty1;
        NGIN::Containers::String empty2;
        empty1.Append(empty2);

        expect(empty1.GetSize() == 0_ul);
        expect(CStrEqual(empty1.CStr(), ""));
    };

    //-------------------------------------------------------------------------
    // Additional/Extended Tests
    //-------------------------------------------------------------------------

    "Reserve_CapacityTest"_test = [] {
        NGIN::Containers::String s("Hi");
        expect(s.GetSize() == 2_ul);

        // Check that reserving below current size does nothing
        s.Reserve(1);
        // Must not shrink
        expect(s.GetSize() == 2_ul);

        // Check that reserving bigger triggers possible reallocation
        s.Reserve(100);
        // The size remains 2, capacity is (conceptually) at least 100
        // but we don't have a direct .Capacity() to check, so just test usage:
        s.Append(" more characters...");
        expect(s.GetSize() > 2_ul);
    };

    "Append_EmptyCStr"_test = [] {
        NGIN::Containers::String s("NonEmpty");
        s.Append("");
        // No change in size
        expect(s.GetSize() == 8_ul);
        expect(CStrEqual(s.CStr(), "NonEmpty"));
    };

    "MultipleAppends"_test = [] {
        NGIN::Containers::String s("ABC");
        s.Append("123");
        s.Append("XYZ");

        expect(s.GetSize() == 9_ul);
        expect(CStrEqual(s.CStr(), "ABC123XYZ"));
    };

    "SelfMoveAssignment"_test = [] {
        NGIN::Containers::String s("SelfMove");
        s = std::move(s);
        // By standard library convention, self-move is allowed but the post-state
        // is not guaranteed. We usually just test that it doesn't crash or lose data.
        // Implementation-defined: let's see if we preserve the data:
        expect(s.GetSize() == 8_ul);
        expect(CStrEqual(s.CStr(), "SelfMove"));
    };

    "NullTermination"_test = [] {
        NGIN::Containers::String s("Test");
        const char*              cstr = s.CStr();
        // Check that string is null terminated
        expect(cstr[s.GetSize()] == '\0');
    };

    "MultipleReserveIncreases"_test = [] {
        NGIN::Containers::String s("Start");
        // Reserve multiple times to increase capacity
        s.Reserve(100);
        s.Reserve(200);
        s.Reserve(150);// Should not reduce capacity
        // Append data to check capacity handling
        s.Append(std::string(180, 'X').c_str());
        expect(s.GetSize() >= 185_ul);// "Start" + 180 'X'
    };

    "CopyChainAssignment"_test = [] {
        NGIN::Containers::String a("First"), b("Second"), c("Third");
        // Chain copy assignment
        a = b = c;
        expect(a.GetSize() == c.GetSize());
        expect(b.GetSize() == c.GetSize());
        expect(CStrEqual(a.CStr(), "Third"));
        expect(CStrEqual(b.CStr(), "Third"));
    };

    "MoveChainAssignment"_test = [] {
        NGIN::Containers::String a("Alpha"), b("Beta"), c("Gamma");
        // Chain move assignment
        a = std::move(b) = std::move(c);
        // a should now be "Gamma", and b and c are in valid but unspecified states
        expect(a.GetSize() == 5_ul);
        expect(CStrEqual(a.CStr(), "Gamma"));
    };

    "StressTest_LongStringCreation"_test = [] {
        // Create a very long string and perform operations on it
        std::string              longStr(10000, 'Z');
        NGIN::Containers::String s(longStr.c_str());
        expect(s.GetSize() == 10000_ul);
        expect(CStrEqual(s.CStr(), longStr.c_str()));

        // Append additional data to stress append and reallocation
        s.Append(std::string(5000, 'Y').c_str());
        expect(s.GetSize() == 15000_ul);
    };

    "RepeatedReserveWithoutAppend"_test = [] {
        NGIN::Containers::String s("Initial");
        // Repeatedly reserve larger capacities without actual appending
        for (NGIN::UIntSize cap = 100; cap <= 1000; cap += 100)
        {
            s.Reserve(cap);
            // Ensure content remains unchanged
            expect(CStrEqual(s.CStr(), "Initial"));
        }
        expect(s.GetSize() == 7_ul);
    };
};
