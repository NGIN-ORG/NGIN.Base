/// @file DynamicLibrary.hpp
/// @brief Declaration and inline implementation of the DynamicLibrary class.
/// @details
/// A cross-platform, header-only abstraction for handling dynamic/shared libraries.
/// This class provides functionalities to load, unload, and resolve symbols from
/// dynamic libraries (.dll on Windows, .so on Linux, and .dylib on macOS).

#pragma once

#include <stdexcept>
#include <string>
#include <utility>

#if defined(_WIN32) || defined(_WIN64)
#include <windows.h>
using LibraryHandle = HMODULE;
#else
#include <dlfcn.h>
using LibraryHandle = void*;
#endif

namespace NGIN
{
    namespace IO
    {
        /// @class DynamicLibrary
        /// @brief A cross-platform abstraction for handling dynamic/shared libraries.
        ///
        /// @details
        /// The DynamicLibrary class provides a simple interface to load and unload
        /// dynamic libraries and resolve symbols within them. It abstracts away platform-specific
        /// details, offering a consistent API across Windows, Linux, and macOS.
        ///
        /// Example usage:
        /// @code
        /// #include "DynamicLibrary.hpp"
        ///
        /// int main()
        /// {
        ///     try
        ///     {
        ///         NGIN::System::DynamicLibrary library("example.dll");
        ///         library.load();
        ///
        ///         using FunctionType = int(*)(int, int);
        ///         auto exampleFunction = library.resolve<FunctionType>("exampleFunction");
        ///
        ///         int result = exampleFunction(2, 3);
        ///         std::cout << "Result: " << result << std::endl;
        ///
        ///         library.unload();
        ///     }
        ///     catch (const std::exception& ex)
        ///     {
        ///         std::cerr << "Error: " << ex.what() << std::endl;
        ///     }
        ///
        ///     return 0;
        /// }
        /// @endcode
        class DynamicLibrary
        {
        public:
            /// @brief Constructs a DynamicLibrary object with the specified library path.
            /// @param libraryPath The path to the dynamic/shared library.
            /// @throws std::invalid_argument if the libraryPath is empty.
            explicit DynamicLibrary(std::string libraryPath);

            /// @brief Destructs the DynamicLibrary object and unloads the library if loaded.
            /// @details Ensures that the loaded library is properly unloaded upon destruction
            /// to prevent resource leaks.
            ~DynamicLibrary();

            /// @brief Loads the dynamic/shared library.
            /// @details Attempts to load the library from the specified path. If the library
            /// is already loaded, an exception is thrown.
            /// @throws std::runtime_error if the library is already loaded or cannot be loaded.
            void Load();

            /// @brief Unloads the dynamic/shared library.
            /// @details Attempts to unload the library. If the library is not loaded, an exception
            /// is thrown.
            /// @throws std::runtime_error if the library is not loaded or cannot be unloaded.
            void Unload();

            /// @brief Resolves a symbol in the dynamic/shared library.
            /// @tparam T The type of the symbol (typically a function pointer type).
            /// @param symbolName The name of the symbol to resolve.
            /// @return A pointer to the resolved symbol of type T.
            /// @throws std::runtime_error if the library is not loaded or the symbol cannot be resolved.
            template<typename T>
            T* Resolve(const std::string& symbolName);

            /// @brief Checks if the library is currently loaded.
            /// @return `true` if the library is loaded; otherwise, `false`.
            [[nodiscard]] bool IsLoaded() const noexcept;

            /// @brief Retrieves the path of the dynamic/shared library.
            /// @return A constant reference to the library path string.
            [[nodiscard]] const std::string& GetPath() const noexcept;

        private:
            /// @brief The path to the dynamic/shared library.
            /// @todo use an NGIN Path object instead of std::string
            std::string libraryPath;///< The path to the dynamic/shared library.
            LibraryHandle handle;   ///< The platform-specific handle to the library.

            /// @brief Prevent copying of DynamicLibrary objects.
            DynamicLibrary(const DynamicLibrary&) = delete;

            /// @brief Prevent assignment of DynamicLibrary objects.
            DynamicLibrary& operator=(const DynamicLibrary&) = delete;
        };

        ////////////////////////////////////////////////////////////////////////////////
        // Implementation
        ////////////////////////////////////////////////////////////////////////////////

        /// @brief Constructs a DynamicLibrary object with the specified library path.
        /// @param libraryPath The path to the dynamic/shared library.
        inline DynamicLibrary::DynamicLibrary(std::string libraryPath)
            : libraryPath(std::move(libraryPath)), handle(nullptr)
        {
            if (this->libraryPath.empty())
            {
                throw std::invalid_argument("DynamicLibrary: libraryPath cannot be empty.");
            }
        }

        /// @brief Destructs the DynamicLibrary object and unloads the library if loaded.
        inline DynamicLibrary::~DynamicLibrary()
        {
            if (handle)
            {
                try
                {
                    Unload();
                } catch (const std::exception& ex)
                {
                    // Log the error or handle it as appropriate.
                    // Avoid throwing exceptions from destructors.
                    // For example, you could use a logging mechanism here.
                }
            }
        }

        /// @brief Loads the dynamic/shared library.
        inline void DynamicLibrary::Load()
        {
            if (handle)
            {
                throw std::runtime_error("DynamicLibrary::load: Library already loaded.");
            }

#if defined(_WIN32) || defined(_WIN64)
            handle = LoadLibraryA(libraryPath.c_str());
            if (!handle)
            {
                throw std::runtime_error("DynamicLibrary::load: Failed to load library: " + libraryPath);
            }
#else
            handle = dlopen(libraryPath.c_str(), RTLD_LAZY);
            if (!handle)
            {
                throw std::runtime_error("DynamicLibrary::load: Failed to load library: " + libraryPath + " (" + dlerror() + ")");
            }
#endif
        }

        /// @brief Unloads the dynamic/shared library.
        inline void DynamicLibrary::Unload()
        {
            if (!handle)
            {
                throw std::runtime_error("DynamicLibrary::unload: Library not loaded.");
            }

#if defined(_WIN32) || defined(_WIN64)
            if (!FreeLibrary(handle))
            {
                throw std::runtime_error("DynamicLibrary::unload: Failed to unload library: " + libraryPath);
            }
#else
            if (dlclose(handle) != 0)
            {
                throw std::runtime_error("DynamicLibrary::unload: Failed to unload library: " + libraryPath + " (" + dlerror() + ")");
            }
#endif
            handle = nullptr;
        }

        /// @brief Resolves a symbol in the dynamic/shared library.
        /// @tparam T The type of the symbol.
        /// @param symbolName The name of the symbol to resolve.
        /// @return A pointer to the resolved symbol.
        template<typename T>
        inline T* DynamicLibrary::Resolve(const std::string& symbolName)
        {
            if (!handle)
            {
                throw std::runtime_error("DynamicLibrary::resolve: Library not loaded.");
            }

#if defined(_WIN32) || defined(_WIN64)
            FARPROC symbol = GetProcAddress(handle, symbolName.c_str());
            if (!symbol)
            {
                throw std::runtime_error("DynamicLibrary::resolve: Failed to resolve symbol: " + symbolName);
            }
            return reinterpret_cast<T*>(symbol);
#else
            dlerror();// Clear any existing error
            void* symbol      = dlsym(handle, symbolName.c_str());
            const char* error = dlerror();
            if (error != nullptr)
            {
                throw std::runtime_error("DynamicLibrary::resolve: Failed to resolve symbol: " + symbolName + " (" + error + ")");
            }
            return reinterpret_cast<T*>(symbol);
#endif
        }

        /// @brief Checks if the library is currently loaded.
        /// @return `true` if the library is loaded; otherwise, `false`.
        inline bool DynamicLibrary::IsLoaded() const noexcept
        {
            return handle != nullptr;
        }

        /// @brief Retrieves the path of the dynamic/shared library.
        /// @return A constant reference to the library path string.
        inline const std::string& DynamicLibrary::GetPath() const noexcept
        {
            return libraryPath;
        }

    }// namespace IO
}// namespace NGIN
