/// @file VectorTest.cpp
/// @brief Tests for NGIN::Containers::Vector using boost::ut
/// @details
/// This file contains a series of tests to verify the correctness of the
/// Vector container in various scenarios, including default construction,
/// copy/move semantics, insertion, emplacing, removal, and capacity management.
/// We use boost::ut for our unit testing framework.

#include <NGIN/Containers/Vector.hpp>
#include <algorithm>
#include <boost/ut.hpp>
#include <string>
#include <vector>

using namespace boost::ut;

namespace
{
    /// @brief Helper function to generate a string of length n with a given character c.
    std::string GenerateString(std::size_t length, char c)
    {
        return std::string(length, c);
    }

    /// @brief Simple struct to test non-POD behavior in Vector.
    struct NonPod
    {
        int data;
        char* buffer;

        explicit NonPod(int val = 0)
            : data(val), buffer(new char[4] {'T', 'e', 's', 't'})
        {
        }

        NonPod(const NonPod& other)
            : data(other.data), buffer(new char[4])
        {
            std::copy(other.buffer, other.buffer + 4, buffer);
        }

        NonPod(NonPod&& other) noexcept
            : data(other.data), buffer(other.buffer)
        {
            other.buffer = nullptr;
        }

        ~NonPod()
        {
            delete[] buffer;
        }

        NonPod& operator=(const NonPod& other)
        {
            if (this == &other)
                return *this;

            data = other.data;
            delete[] buffer;
            buffer = new char[4];
            std::copy(other.buffer, other.buffer + 4, buffer);
            return *this;
        }

        NonPod& operator=(NonPod&& other) noexcept
        {
            if (this == &other)
                return *this;

            data = other.data;
            delete[] buffer;
            buffer       = other.buffer;
            other.buffer = nullptr;
            return *this;
        }
    };

    /// @brief A helper type to confirm that insertion at the front works correctly.
    struct TrackedInt
    {
        int value;

        explicit TrackedInt(int v = 0)
            : value(v) {}
        TrackedInt(const TrackedInt&)                = default;
        TrackedInt(TrackedInt&&) noexcept            = default;
        TrackedInt& operator=(const TrackedInt&)     = default;
        TrackedInt& operator=(TrackedInt&&) noexcept = default;
        ~TrackedInt()                                = default;
    };

}// end anonymous namespace

suite<"NGIN::Containers::Vector"> tests = [] {
    //--------------------------------------------------------------------------------
    "DefaultConstruction"_test = [] {
        NGIN::Containers::Vector<int> vec;
        expect(vec.Size() == 0_ul);
        expect(vec.Capacity() == 0_ul);
    };

    "InitializeWithCapacity"_test = [] {
        NGIN::Containers::Vector<int> vec(10);
        expect(vec.Size() == 0_ul);
        expect(vec.Capacity() >= 10_ul);
    };

    //--------------------------------------------------------------------------------
    "PushingIntegers"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);
        vec.PushBack(3);

        expect(vec.Size() == 3_ul);
        expect(vec[0] == 1_i);
        expect(vec[1] == 2_i);
        expect(vec[2] == 3_i);
    };

    "CapacityGrowsAutomatically"_test = [] {
        NGIN::Containers::Vector<int> vec(2);
        vec.PushBack(10);
        vec.PushBack(20);

        // Should grow internally
        vec.PushBack(30);

        expect(vec.Size() == 3_ul);
        expect(vec[0] == 10_i);
        expect(vec[1] == 20_i);
        expect(vec[2] == 30_i);
        expect(vec.Capacity() >= 3_ul);
    };

    //--------------------------------------------------------------------------------
    "InsertAtValidIndex"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);
        vec.PushBack(4);// Insert '3' at index 2

        vec.PushAt(2, 3);
        expect(vec.Size() == 4_ul);
        expect(vec[2] == 3_i);
        expect(vec[3] == 4_i);
    };

    "InsertAtInvalidIndex"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);

        expect(throws<std::out_of_range>([&] { vec.PushAt(3, 999); }));// out of range
    };

    //--------------------------------------------------------------------------------
    "EmplaceAtIndex"_test = [] {
        NGIN::Containers::Vector<std::string> vec;
        vec.PushBack("A");
        vec.PushBack("B");
        vec.PushBack("D");

        // Emplace 'C' at index 2
        vec.EmplaceAt(2, "C");
        expect(vec.Size() == 4_ul);
        expect(vec[0] == "A");
        expect(vec[1] == "B");
        expect(vec[2] == "C");
        expect(vec[3] == "D");
    };

    "EmplaceAtEnd"_test = [] {
        NGIN::Containers::Vector<std::string> vec;
        vec.EmplaceBack("Hello");
        vec.EmplaceBack("World");
        expect(vec.Size() == 2_ul);
        expect(vec[0] == "Hello");
        expect(vec[1] == "World");
    };

    //--------------------------------------------------------------------------------
    "PopRemovesLast"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(10);
        vec.PushBack(20);
        vec.PushBack(30);

        vec.PopBack();
        expect(vec.Size() == 2_ul);
        expect(vec[0] == 10_i);
        expect(vec[1] == 20_i);

        // Should throw if empty
        vec.PopBack();
        vec.PopBack();
        bool caught = false;
        try
        {
            vec.PopBack();
        } catch (const std::out_of_range&)
        {
            caught = true;
        }
        expect(caught == true);
    };

    "EraseRemovesElement"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(0);
        vec.PushBack(1);
        vec.PushBack(2);
        vec.PushBack(3);

        vec.Erase(1);// remove '1'
        expect(vec.Size() == 3_ul);
        expect(vec[0] == 0_i);
        expect(vec[1] == 2_i);
        expect(vec[2] == 3_i);

        bool caught = false;
        try
        {
            vec.Erase(10);
        } catch (const std::out_of_range&)
        {
            caught = true;
        }
        expect(caught == true);
    };

    //--------------------------------------------------------------------------------
    "ClearingPreservesCapacity"_test = [] {
        NGIN::Containers::Vector<int> vec(5);
        vec.PushBack(10);
        vec.PushBack(20);
        vec.PushBack(30);

        vec.Clear();
        expect(vec.Size() == 0_ul);
        expect(vec.Capacity() >= 5_ul);
    };

    "ReserveIncreasesCapacity"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.Reserve(50);
        expect(vec.Capacity() >= 50_ul);
    };

    //--------------------------------------------------------------------------------
    "AccessWithinRange"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(5);
        vec.PushBack(15);

        expect(vec.At(0) == 5_i);
        expect(vec.At(1) == 15_i);
    };

    "AccessOutOfRange"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(5);

        bool caught = false;
        try
        {
            auto& val = vec.At(1);
            (void) val;
        } catch (const std::out_of_range&)
        {
            caught = true;
        }
        expect(caught == true);
    };

    //--------------------------------------------------------------------------------
    "CopyConstructorCreatesNewVector"_test = [] {
        NGIN::Containers::Vector<int> original;
        original.PushBack(1);
        original.PushBack(2);
        original.PushBack(3);

        NGIN::Containers::Vector<int> copy {original};
        expect(copy.Size() == 3_ul);
        expect(copy[0] == 1_i);
        expect(copy[1] == 2_i);
        expect(copy[2] == 3_i);
    };

    "MoveConstructorTransfersOwnership"_test = [] {
        NGIN::Containers::Vector<std::string> source;
        source.PushBack("Hello");
        source.PushBack("World");

        auto oldPtr = &source[0];
        NGIN::Containers::Vector<std::string> moved(std::move(source));
        expect(moved.Size() == 2_ul);
        expect(moved[0] == "Hello");
        expect(moved[1] == "World");

        expect(source.Size() == 0_ul);
        expect(oldPtr == &moved[0]);
    };

    //--------------------------------------------------------------------------------
    "CopyAssignmentOverwritesData"_test = [] {
        NGIN::Containers::Vector<int> v1;
        v1.PushBack(10);
        v1.PushBack(20);

        NGIN::Containers::Vector<int> v2;
        v2.PushBack(99);

        v2 = v1;
        expect(v2.Size() == 2_ul);
        expect(v2[0] == 10_i);
        expect(v2[1] == 20_i);

        // Confirm deep copy
        v1[0] = 500;
        expect(v2[0] == 10_i);
    };

    "MoveAssignmentTransfersData"_test = [] {
        NGIN::Containers::Vector<std::string> v1;
        v1.PushBack("One");
        v1.PushBack("Two");

        NGIN::Containers::Vector<std::string> v2;
        v2.PushBack("Alpha");
        v2.PushBack("Beta");
        v2.PushBack("Gamma");

        v2 = std::move(v1);
        expect(v2.Size() == 2_ul);
        expect(v2[0] == "One");
        expect(v2[1] == "Two");

        // v1 is now empty
        expect(v1.Size() == 0_ul);
    };

    //--------------------------------------------------------------------------------
    "NonPodBehavior"_test = [] {
        NGIN::Containers::Vector<NonPod> vec;
        vec.PushBack(NonPod(10));
        vec.PushBack(NonPod(20));
        vec.EmplaceBack(30);

        expect(vec.Size() == 3_ul);
        expect(vec[0].data == 10_i);
        expect(vec[1].data == 20_i);
        expect(vec[2].data == 30_i);

        NonPod temp(40);
        vec.PushBack(temp);
        expect(vec[3].data == 40_i);
        expect(vec.Size() == 4_ul);
    };

    "SelfCopyAssignment"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);
        vec.PushBack(3);

        vec = vec;
        expect(vec.Size() == 3_ul);
        expect(vec[0] == 1_i);
        expect(vec[1] == 2_i);
        expect(vec[2] == 3_i);
    };

    "SelfMoveAssignment"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);

        vec = std::move(vec);
        expect(vec.Size() == 2_ul);
        expect(vec[0] == 1_i);
        expect(vec[1] == 2_i);
    };

    //--------------------------------------------------------------------------------
    "InsertManyElements"_test = [] {
        NGIN::Containers::Vector<int> vec;
        const int num = 1000;
        for (int i = 0; i < num; i++)
        {
            vec.PushBack(i);
        }

        expect(vec.Size() == static_cast<std::size_t>(num));
        expect(vec[0] == 0_i);
        expect(vec[500] == 500_i);
        expect(vec[999] == 999_i);
    };

    //--------------------------------------------------------------------------------
    "InsertAtFront"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(100);
        vec.PushBack(200);

        vec.PushAt(0, 50);
        expect(vec.Size() == 3_ul);
        expect(vec[0] == 50_i);
        expect(vec[1] == 100_i);
        expect(vec[2] == 200_i);
    };

    "InsertAtEnd"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(10);
        vec.PushBack(20);

        vec.PushAt(vec.Size(), 30);
        expect(vec.Size() == 3_ul);
        expect(vec[0] == 10_i);
        expect(vec[1] == 20_i);
        expect(vec[2] == 30_i);
    };

    //--------------------------------------------------------------------------------
    "PopUntilEmpty"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(1);
        vec.PushBack(2);

        vec.PopBack();// now size=1
        expect(vec.Size() == 1_ul);

        vec.PopBack();// now size=0
        expect(vec.Size() == 0_ul);

        bool caught = false;
        try
        {
            vec.PopBack();
        } catch (const std::out_of_range&)
        {
            caught = true;
        }
        expect(caught == true);
    };

    "BoundaryChecksForAt"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(10);
        vec.PushBack(20);

        // boundary: index=0
        expect(vec.At(0) == 10_i);
        // boundary: index=1
        expect(vec.At(1) == 20_i);

        bool caught = false;
        try
        {
            vec.At(2);
        } catch (const std::out_of_range&)
        {
            caught = true;
        }
        expect(caught == true);
    };

    //--------------------------------------------------------------------------------
    "ReserveNoShrink"_test = [] {
        NGIN::Containers::Vector<int> vec;
        expect(vec.Capacity() == 0_ul);

        vec.Reserve(10);
        expect(vec.Capacity() >= 10_ul);

        // Should not shrink if asked for a smaller capacity
        vec.Reserve(5);
        expect(vec.Capacity() >= 10_ul);

        // Should grow if asked for more
        vec.Reserve(20);
        expect(vec.Capacity() >= 20_ul);
    };

    "InsertManyIntoReservedVector"_test = [] {
        NGIN::Containers::Vector<int> vec;
        static const int bigCount = 2000;
        for (int i = 0; i < bigCount; ++i)
        {
            vec.PushBack(i);
        }
        expect(vec.Size() == static_cast<std::size_t>(bigCount));

        expect(vec[0] == 0_i);
        expect(vec[999] == 999_i);
        expect(vec[1999] == 1999_i);
    };

    //--------------------------------------------------------------------------------
    "EmplaceAtFront"_test = [] {
        NGIN::Containers::Vector<std::string> vec;
        vec.PushBack("second");
        vec.EmplaceAt(0, "first");
        expect(vec.Size() == 2_ul);
        expect(vec[0] == "first");
        expect(vec[1] == "second");
    };

    "EmplaceAtMiddleReallocation"_test = [] {
        NGIN::Containers::Vector<std::string> vec;
        vec.PushBack("A");
        vec.PushBack("B");
        vec.PushBack("D");

        // Force capacity=3
        vec.Clear();
        vec.Reserve(3);
        vec.PushBack("A");
        vec.PushBack("B");
        vec.PushBack("D");

        // Now capacity is used up; next insertion forces re-allocation
        vec.EmplaceAt(2, "C");
        expect(vec.Size() == 4_ul);
        expect(vec[0] == "A");
        expect(vec[1] == "B");
        expect(vec[2] == "C");
        expect(vec[3] == "D");
    };

    //--------------------------------------------------------------------------------
    "SubscriptForWriting"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.PushBack(10);
        vec.PushBack(20);

        vec[0] = 99;
        expect(vec[0] == 99_i);

        vec[1] = 100;
        expect(vec[1] == 100_i);
    };

    "CapacityStaysAfterRemoval"_test = [] {
        NGIN::Containers::Vector<int> vec;
        vec.Reserve(8);
        auto cap = vec.Capacity();
        expect(cap >= 8_ul);

        vec.PushBack(1);
        vec.PushBack(2);
        vec.PushBack(3);
        expect(vec.Size() == 3_ul);

        // remove all
        vec.Clear();
        expect(vec.Size() == 0_ul);
        expect(vec.Capacity() == cap);

        // reuse
        vec.PushBack(10);
        vec.PushBack(20);
        expect(vec.Size() == 2_ul);
        expect(vec.Capacity() == cap);
    };

    //--------------------------------------------------------------------------------
    "NonPodFrontInsertion"_test = [] {
        NGIN::Containers::Vector<TrackedInt> vec;
        // Insert 3, 2, 1 at front => final order: 1, 2, 3
        vec.PushAt(0, TrackedInt(3));
        vec.PushAt(0, TrackedInt(2));
        vec.PushAt(0, TrackedInt(1));

        expect(vec.Size() == 3_ul);
        expect(vec[0].value == 1_i);
        expect(vec[1].value == 2_i);
        expect(vec[2].value == 3_i);
    };

    "NonPodLargeInsert"_test = [] {
        NGIN::Containers::Vector<TrackedInt> vec;
        static constexpr int largeCount = 1024;
        for (int i = 0; i < largeCount; ++i)
        {
            vec.PushBack(TrackedInt(i));
        }

        expect(vec.Size() == static_cast<std::size_t>(largeCount));
        expect(vec[0].value == 0_i);
        expect(vec[largeCount - 1].value == (largeCount - 1));
    };
};
