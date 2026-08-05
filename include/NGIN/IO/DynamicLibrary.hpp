/// @file DynamicLibrary.hpp
/// @brief Cross-platform abstraction for dynamic/shared libraries.

#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/IO/Path.hpp>

#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace NGIN::IO
{
    /// @brief Runtime error raised when dynamic library load or symbol resolution fails.
    class NGIN_IO_API DynamicLibraryError final : public std::runtime_error
    {
    public:
        /// @brief Constructs an error with platform or symbol-resolution context.
        explicit DynamicLibraryError(const std::string& message)
            : std::runtime_error(message)
        {
        }
    };

    /// @brief Movable RAII wrapper around a native dynamic library handle.
    ///
    /// @details
    /// - Instances own at most one loaded library and are not copyable.
    /// - `Load()` is idempotent and uses the constructor-selected binding mode.
    /// - `Unload()` is idempotent, best-effort, and never throws.
    /// - Any resolved symbol becomes invalid after `Unload()` or destruction.
    /// - `Resolve<T>()` expects `T` to be the exact object or function pointer type, including ABI.
    /// - Concurrent `Load()`, `Unload()`, and `Resolve()` on the same instance are not synchronized.
    class NGIN_IO_API DynamicLibrary
    {
    public:
        enum class LoadMode
        {
            /// @brief Construct unloaded and bind lazily when `Load()` is called where the platform supports it.
            Lazy,
            /// @brief Load during construction and bind eagerly where the platform supports it.
            Now,
        };

        /// @brief Constructs a wrapper and loads immediately when @p loadMode is `Now`.
        /// @throws DynamicLibraryError if immediate loading fails.
        explicit DynamicLibrary(Path libraryPath, LoadMode loadMode = LoadMode::Now);
        /// @brief Constructs a wrapper from UTF-8 path text and optionally loads it immediately.
        /// @throws DynamicLibraryError if immediate loading fails.
        explicit DynamicLibrary(std::string_view libraryPath, LoadMode loadMode = LoadMode::Now);
        /// @brief Unloads the library if necessary.
        ~DynamicLibrary() noexcept;

        /// @brief Transfers the native library handle from another wrapper.
        DynamicLibrary(DynamicLibrary&& other) noexcept;
        /// @brief Unloads this library and transfers the handle from another wrapper.
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        /// @brief Loads the selected library if it is not already loaded.
        /// @throws DynamicLibraryError when the platform loader rejects the library.
        void Load();
        /// @brief Unloads the library and invalidates all previously resolved symbols.
        void Unload() noexcept;

        /// @brief Constructs and returns a loaded dynamic library.
        [[nodiscard]] static DynamicLibrary Open(Path libraryPath, LoadMode loadMode = LoadMode::Now);
        /// @brief Constructs and returns a loaded dynamic library from UTF-8 path text.
        [[nodiscard]] static DynamicLibrary Open(std::string_view libraryPath, LoadMode loadMode = LoadMode::Now);

        /// @brief Resolves a required object or function pointer by exported symbol name.
        /// @throws DynamicLibraryError when the library is unloaded or the symbol is absent.
        template<typename T>
        [[nodiscard]] T Resolve(std::string_view symbolName) const
        {
            static_assert(std::is_pointer_v<T>, "DynamicLibrary::Resolve<T> requires T to be a pointer type.");
            return reinterpret_cast<T>(ResolveRaw(symbolName));
        }

        /// @brief Attempts to resolve an exported object or function pointer.
        /// @return The pointer, or an empty optional when the library or symbol is unavailable.
        template<typename T>
        [[nodiscard]] std::optional<T> TryResolve(std::string_view symbolName) const
        {
            static_assert(std::is_pointer_v<T>, "DynamicLibrary::TryResolve<T> requires T to be a pointer type.");

            if (void* symbol = TryResolveRaw(symbolName))
            {
                return reinterpret_cast<T>(symbol);
            }

            return std::nullopt;
        }

        /// @brief Returns whether a native library handle is currently loaded.
        [[nodiscard]] bool IsLoaded() const noexcept;
        /// @brief Returns the configured library path.
        [[nodiscard]] const Path& GetPath() const noexcept;
        /// @brief Returns the configured binding mode.
        [[nodiscard]] LoadMode GetLoadMode() const noexcept;

    private:
        [[nodiscard]] void*              ResolveRaw(std::string_view symbolName) const;
        [[nodiscard]] void*              TryResolveRaw(std::string_view symbolName) const;
        [[nodiscard]] static std::string GetLastPlatformError();

        Path     m_libraryPath {};
        LoadMode m_loadMode {LoadMode::Now};
        void*    m_handle {nullptr};

        DynamicLibrary(const DynamicLibrary&)            = delete;
        DynamicLibrary& operator=(const DynamicLibrary&) = delete;
    };
}// namespace NGIN::IO
