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
    class NGIN_BASE_API DynamicLibraryError final : public std::runtime_error
    {
    public:
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
    class NGIN_BASE_API DynamicLibrary
    {
    public:
        enum class LoadMode
        {
            /// @brief Construct unloaded and bind lazily when `Load()` is called where the platform supports it.
            Lazy,
            /// @brief Load during construction and bind eagerly where the platform supports it.
            Now,
        };

        explicit DynamicLibrary(Path libraryPath, LoadMode loadMode = LoadMode::Now);
        explicit DynamicLibrary(std::string_view libraryPath, LoadMode loadMode = LoadMode::Now);
        ~DynamicLibrary() noexcept;

        DynamicLibrary(DynamicLibrary&& other) noexcept;
        DynamicLibrary& operator=(DynamicLibrary&& other) noexcept;

        void Load();
        void Unload() noexcept;

        [[nodiscard]] static DynamicLibrary Open(Path libraryPath, LoadMode loadMode = LoadMode::Now);
        [[nodiscard]] static DynamicLibrary Open(std::string_view libraryPath, LoadMode loadMode = LoadMode::Now);

        template<typename T>
        [[nodiscard]] T Resolve(std::string_view symbolName) const
        {
            static_assert(std::is_pointer_v<T>, "DynamicLibrary::Resolve<T> requires T to be a pointer type.");
            return reinterpret_cast<T>(ResolveRaw(symbolName));
        }

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

        [[nodiscard]] bool        IsLoaded() const noexcept;
        [[nodiscard]] const Path& GetPath() const noexcept;
        [[nodiscard]] LoadMode    GetLoadMode() const noexcept;

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
