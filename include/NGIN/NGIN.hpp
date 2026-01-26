#pragma once
#include <NGIN/Containers/HashMap.hpp>
#include <NGIN/Text/String.hpp>
#include <NGIN/Containers/Vector.hpp>
#include <NGIN/Defines.hpp>
#include <NGIN/Exceptions/Exception.hpp>
#include <NGIN/IO/DynamicLibrary.hpp>
#include <NGIN/Memory/AllocationHelpers.hpp>
#include <NGIN/Memory/AllocatorConcept.hpp>
#include <NGIN/Memory/FallbackAllocator.hpp>
#include <NGIN/Memory/HalfPointer.hpp>
#include <NGIN/Memory/LinearAllocator.hpp>
#include <NGIN/Memory/SystemAllocator.hpp>
#include <NGIN/Memory/TrackingAllocator.hpp>
#include <NGIN/Meta/EnumTraits.hpp>
#include <NGIN/Meta/FunctionTraits.hpp>
#include <NGIN/Meta/TypeId.hpp>
#include <NGIN/Meta/TypeTraits.hpp>
#include <NGIN/Primitives.hpp>
#include <NGIN/Serialization/Archive.hpp>
#include <NGIN/Serialization/JSON/JsonArchive.hpp>
#include <NGIN/Serialization/JSON/JsonParser.hpp>
#include <NGIN/Serialization/XML/XmlArchive.hpp>
#include <NGIN/Serialization/XML/XmlParser.hpp>
#include <NGIN/Utilities/Any.hpp>
#include <NGIN/Utilities/Callable.hpp>
#include <NGIN/Utilities/Expected.hpp>
#include <NGIN/Utilities/LSBFlag.hpp>
#include <NGIN/Utilities/MSBFlag.hpp>
#include <NGIN/Utilities/Optional.hpp>
#include <NGIN/Utilities/StringInterner.hpp>


//#include<NGIN / Benchmark.hpp>
#include <NGIN/Timer.hpp>
#include <NGIN/Units.hpp>
