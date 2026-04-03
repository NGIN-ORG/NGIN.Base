#include <NGIN/IO/DynamicLibrary.hpp>

#include <Windows.h>

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
        m_handle              = ::LoadLibraryA(nativePath.CStr());
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

        HMODULE handle = static_cast<HMODULE>(m_handle);
        m_handle       = nullptr;
        ::FreeLibrary(handle);
    }

    void* DynamicLibrary::ResolveRaw(std::string_view symbolName) const
    {
        if (m_handle == nullptr)
        {
            throw DynamicLibraryError(
                    MakeResolveMessage("Cannot resolve symbol because library is not loaded", m_libraryPath, symbolName, {}));
        }

        const std::string symbolStorage = MakeSymbolStorage(symbolName);
        ::SetLastError(ERROR_SUCCESS);
        FARPROC const symbol = ::GetProcAddress(static_cast<HMODULE>(m_handle), symbolStorage.c_str());
        if (symbol == nullptr)
        {
            throw DynamicLibraryError(
                    MakeResolveMessage("Failed to resolve symbol", m_libraryPath, symbolName, GetLastPlatformError()));
        }

        return reinterpret_cast<void*>(symbol);
    }

    void* DynamicLibrary::TryResolveRaw(std::string_view symbolName) const
    {
        if (m_handle == nullptr)
        {
            return nullptr;
        }

        const std::string symbolStorage = MakeSymbolStorage(symbolName);
        return reinterpret_cast<void*>(
                ::GetProcAddress(static_cast<HMODULE>(m_handle), symbolStorage.c_str()));
    }

    std::string DynamicLibrary::GetLastPlatformError()
    {
        const DWORD error = ::GetLastError();
        if (error == ERROR_SUCCESS)
        {
            return "unknown dynamic loader error";
        }

        LPSTR       messageBuffer = nullptr;
        const DWORD size          = ::FormatMessageA(FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                                             FORMAT_MESSAGE_FROM_SYSTEM |
                                                             FORMAT_MESSAGE_IGNORE_INSERTS,
                                                     nullptr,
                                                     error,
                                                     MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
                                                     reinterpret_cast<LPSTR>(&messageBuffer),
                                                     0,
                                                     nullptr);

        std::string message = (size != 0U && messageBuffer != nullptr)
                                      ? std::string(messageBuffer, static_cast<std::size_t>(size))
                                      : "unknown dynamic loader error";

        if (messageBuffer != nullptr)
        {
            ::LocalFree(messageBuffer);
        }

        while (!message.empty() && (message.back() == '\r' || message.back() == '\n'))
        {
            message.pop_back();
        }

        return message;
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
