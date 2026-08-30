#pragma once

/// @file IO.hpp
/// @brief Umbrella include for paths, filesystems, byte readers, dynamic libraries, and processes.

#include <NGIN/IO/AsyncDirectoryHandle.hpp>
#include <NGIN/IO/AsyncFileHandle.hpp>
#include <NGIN/IO/AtomicWriteOptions.hpp>
#include <NGIN/IO/DirectoryEnumerator.hpp>
#include <NGIN/IO/DirectoryHandle.hpp>
#include <NGIN/IO/DynamicLibrary.hpp>
#include <NGIN/IO/FileHandle.hpp>
#include <NGIN/IO/FileSystemDriver.hpp>
#include <NGIN/IO/FileSystemTypes.hpp>
#include <NGIN/IO/FileSystemUtilities.hpp>
#include <NGIN/IO/FileView.hpp>
#include <NGIN/IO/IAsyncFileSystem.hpp>
#include <NGIN/IO/IByteReader.hpp>
#include <NGIN/IO/IDirectoryEnumerator.hpp>
#include <NGIN/IO/IDirectoryHandle.hpp>
#include <NGIN/IO/IFileHandle.hpp>
#include <NGIN/IO/IFileSystem.hpp>
#include <NGIN/IO/IOError.hpp>
#include <NGIN/IO/IOResult.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/MemoryReader.hpp>
#include <NGIN/IO/Path.hpp>
#include <NGIN/IO/Process.hpp>
#include <NGIN/IO/ProcessError.hpp>
#include <NGIN/IO/ProcessOptions.hpp>
#include <NGIN/IO/ProcessResult.hpp>
#include <NGIN/IO/VirtualFileSystem.hpp>
