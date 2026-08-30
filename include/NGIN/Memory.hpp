#pragma once

/// @file Memory.hpp
/// @brief Umbrella include for allocators, storage, object pools, and smart pointers.

#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/AllocationStats.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/AllocatorRef.hpp>
#include <NGIN/Memory/DebugAllocator.hpp>
#include <NGIN/Memory/EpochReclaimer.hpp>
#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/FixedBlockAllocator.hpp>
#include <NGIN/Memory/HalfPointer.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/ObjectPool.hpp>
#include <NGIN/Memory/PolyAllocatorRef.hpp>
#include <NGIN/Memory/SegregatedPoolAllocator.hpp>
#include <NGIN/Memory/SmartPointers.hpp>
#include <NGIN/Memory/StorageFor.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/ThreadSafeAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Memory/UnionStorageFor.hpp>
