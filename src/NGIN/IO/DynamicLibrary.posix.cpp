#include <NGIN/IO/DynamicLibrary.hpp>

#include <dlfcn.h>

#include <string>
#include <utility>

namespace NGIN::IO
{
    namespace
    {
        [[nodiscard]] std::string MakeLibraryMessage(std::string_view action, const Path& path, std::string_view detail)
        {
            std::string message(action);
            message += " '";
            message += path.View();
            message += "'";
            if (!detail.empty())
            {
                message += ": ";
                message += detail;
            }
            return message;
        }

        [[nodiscard]] std::string MakeResolveMessage(std::string_view action,
                                                     const Path&      path,
                                                     std::string_view symbolName,
                                                     std::string_view detail)
        {
            std::string message(action);
            message += " '";
            message += symbolName;
            message += "' from '";
            message += path.View();
            message += "'";
            if (!detail.empty())
            {
                message += ": ";
                message += detail;
            }
            return message;
        }

        [[nodiscard]] std::string MakeSymbolStorage(std::string_view symbolName)
        {
            if (symbolName.empty())
            {
                throw std::invalid_argument("DynamicLibrary: symbol name cannot be empty.");
            }

            return std::string(symbolName);
        }

        [[nodiscard]] int ToNativeLoadFlags(DynamicLibrary::LoadMode mode) noexcept
        {
            return RTLD_LOCAL | (mode == DynamicLibrary::LoadMode::Lazy ? RTLD_LAZY : RTLD_NOW);
        }
    }// namespace

    DynamicLibrary::DynamicLibrary(Path libraryPath, LoadMode loadMode)
        : m_libraryPath(std::move(libraryPath)), m_loadMode(loadMode)
    {
        if (m_libraryPath.IsEmpty())
        {
            throw std::invalid_argument("DynamicLibrary: path cannot be empty.");
        }

        if (m_loadMode == LoadMode::Now)
        {
            Load();
        }
    }

    DynamicLibrary::DynamicLibrary(std::string_view libraryPath, LoadMode loadMode)
        : DynamicLibrary(Path(libraryPath), loadMode)
    {
    }

    DynamicLibrary::~DynamicLibrary() noexcept
    {
        Unload();
    }

    DynamicLibrary::DynamicLibrary(DynamicLibrary&& other) noexcept
        : m_libraryPath(std::move(other.m_libraryPath)), m_loadMode(other.m_loadMode), m_handle(std::exchange(other.m_handle, nullptr))
    {
    }

    DynamicLibrary& DynamicLibrary::operator=(DynamicLibrary&& other) noexcept
    {
        if (this != &other)
        {
            Unload();
            m_libraryPath = std::move(other.m_libraryPath);
            m_loadMode    = other.m_loadMode;
            m_handle      = std::exchange(other.m_handle, nullptr);
        }

        return *this;
    }

    DynamicLibrary DynamicLibrary::Open(Path libraryPath, LoadMode loadMode)
    {
        DynamicLibrary library(std::move(libraryPath), loadMode);
        library.Load();
        return library;
    }

    DynamicLibrary DynamicLibrary::Open(std::string_view libraryPath, LoadMode loadMode)
    {
        return Open(Path(libraryPath), loadMode);
    }

    void DynamicLibrary::Load()
    {
        if (m_handle != nullptr)
        {
            return;
        }

        const auto nativePath = m_libraryPath.ToNative();
        m_handle              = ::dlopen(nativePath.CStr(), ToNativeLoadFlags(m_loadMode));
        if (m_handle == nullptr)
        {
            throw DynamicLibraryError(
                    MakeLibraryMessage("Failed to load library", m_libraryPath, GetLastPlatformError()));
        }
    }

    void DynamicLibrary::Unload() noexcept
    {
        if (m_handle == nullptr)
        {
            return;
        }

        void* handle = std::exchange(m_handle, nullptr);
        ::dlclose(handle);
    }

    void* DynamicLibrary::ResolveRaw(std::string_view symbolName) const
    {
        if (m_handle == nullptr)
        {
            throw DynamicLibraryError(
                    MakeResolveMessage("Cannot resolve symbol because library is not loaded", m_libraryPath, symbolName, {}));
        }

        const std::string symbolStorage = MakeSymbolStorage(symbolName);
        ::dlerror();
        void* const symbol = ::dlsym(m_handle, symbolStorage.c_str());
        if (const char* error = ::dlerror(); error != nullptr)
        {
            throw DynamicLibraryError(
                    MakeResolveMessage("Failed to resolve symbol", m_libraryPath, symbolName, error));
        }

        return symbol;
    }

    void* DynamicLibrary::TryResolveRaw(std::string_view symbolName) const
    {
        if (m_handle == nullptr)
        {
            return nullptr;
        }

        const std::string symbolStorage = MakeSymbolStorage(symbolName);
        ::dlerror();
        void* const symbol = ::dlsym(m_handle, symbolStorage.c_str());
        if (::dlerror() != nullptr)
        {
            return nullptr;
        }

        return symbol;
    }

    std::string DynamicLibrary::GetLastPlatformError()
    {
        if (const char* error = ::dlerror(); error != nullptr)
        {
            return std::string(error);
        }

        return "unknown dynamic loader error";
    }

    bool DynamicLibrary::IsLoaded() const noexcept
    {
        return m_handle != nullptr;
    }

    const Path& DynamicLibrary::GetPath() const noexcept
    {
        return m_libraryPath;
    }

    DynamicLibrary::LoadMode DynamicLibrary::GetLoadMode() const noexcept
    {
        return m_loadMode;
    }
}// namespace NGIN::IO
