#pragma once

/// @file Core.hpp
/// @brief Umbrella include for shared serialization and parsing contracts.

#include <NGIN/Serialization/Core/IncrementalParse.hpp>
#include <NGIN/Serialization/Core/InputCursor.hpp>
#include <NGIN/Serialization/Core/ParseDiagnostic.hpp>
#include <NGIN/Serialization/Core/ParseError.hpp>
#include <NGIN/Serialization/Core/ParseLimits.hpp>
#include <NGIN/Serialization/Core/ParseResources.hpp>
#include <NGIN/Serialization/Core/ParseScratch.hpp>
#include <NGIN/Serialization/Core/SegmentedArena.hpp>
#include <NGIN/Serialization/Core/SourceBuffer.hpp>
#include <NGIN/Serialization/Core/SourceMap.hpp>
#include <NGIN/Serialization/Core/SourceSpan.hpp>
#include <NGIN/Serialization/Core/TextSink.hpp>
