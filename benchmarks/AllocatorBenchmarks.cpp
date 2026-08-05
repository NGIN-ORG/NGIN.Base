#include <NGIN/Benchmark.hpp>
#include <NGIN/Memory/FixedBlockAllocator.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SegregatedPoolAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Units.hpp>

#include <array>
#include <cstddef>
#include <iostream>

using namespace NGIN;

int main()
{
    constexpr std::size_t OperationCount = 1024;

    Benchmark::Register([](BenchmarkContext& context) {
        Memory::SystemAllocator           allocator;
        std::array<void*, OperationCount> pointers {};
        context.start();
        for (auto& pointer: pointers)
            pointer = allocator.Allocate(64, 16);
        for (auto* pointer: pointers)
            allocator.Deallocate(pointer, 64, 16);
        context.stop();
    },
                        "SystemAllocator 1024 x 64-byte allocate/free");

    Benchmark::Register([](BenchmarkContext& context) {
        Memory::FixedBlockAllocator<64, OperationCount, 16> allocator;
        std::array<void*, OperationCount>                   pointers {};
        context.start();
        for (auto& pointer: pointers)
            pointer = allocator.Allocate(64, 16);
        for (auto* pointer: pointers)
            allocator.Deallocate(pointer, 64, 16);
        context.stop();
    },
                        "FixedBlockAllocator 1024 x 64-byte allocate/free");

    Benchmark::Register([](BenchmarkContext& context) {
        Memory::SegregatedPoolAllocator<OperationCount> allocator;
        std::array<void*, OperationCount>               pointers {};
        context.start();
        for (std::size_t index = 0; index < pointers.size(); ++index)
            pointers[index] = allocator.Allocate(16u << (index % 6u), alignof(std::max_align_t));
        for (std::size_t index = 0; index < pointers.size(); ++index)
            allocator.Deallocate(pointers[index], 16u << (index % 6u), alignof(std::max_align_t));
        context.stop();
    },
                        "SegregatedPoolAllocator mixed small allocate/free");

    Benchmark::Register([](BenchmarkContext& context) {
        Memory::LinearAllocator<> allocator(OperationCount * 80);
        context.start();
        for (std::size_t index = 0; index < OperationCount; ++index)
            context.doNotOptimize(allocator.Allocate(64, 16));
        allocator.Reset();
        context.stop();
    },
                        "LinearAllocator 1024 x 64-byte allocate/reset");

    Benchmark::defaultConfig.iterations       = 25;
    Benchmark::defaultConfig.warmupIterations = 5;
    const auto results                        = Benchmark::RunAll<Units::Nanoseconds>();
    Benchmark::PrintSummaryTable(std::cout, results);
    return 0;
}
