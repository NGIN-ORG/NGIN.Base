#pragma once

#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/IO/FileSystemDriver.hpp>

#include <cstddef>
#include <cstdint>

namespace NGIN::IO::detail
{
    enum class NativeFileOperationKind : UInt8
    {
        Read,
        Write,
        Flush,
        Close,
    };

    struct NativeFileCompletion
    {
        enum class Status : UInt8
        {
            Completed,
            Fault,
        };

        Status                    status {Status::Fault};
        Int64                     value {0};
        int                       systemCode {0};
        NGIN::Async::AsyncFault   fault {};
    };

    using NativeFileCompletionCallback = void (*)(void* userData, NativeFileCompletion completion) noexcept;

    struct NativeFileRequest
    {
        NativeFileOperationKind      kind {NativeFileOperationKind::Read};
        std::uintptr_t               handleValue {0};
        UInt64                       offset {0};
        bool                         useCurrentOffset {false};
        void*                        buffer {nullptr};
        UInt32                       size {0};
        void*                        userData {nullptr};
        NativeFileCompletionCallback completion {nullptr};
    };

    class NativeFileBackend
    {
    public:
        virtual ~NativeFileBackend() = default;

        [[nodiscard]] virtual FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept = 0;
        [[nodiscard]] virtual bool Submit(NativeFileRequest request) noexcept = 0;
    };

    [[nodiscard]] std::unique_ptr<NativeFileBackend> CreateNativeFileBackend(const FileSystemDriver::Options& options);
    [[nodiscard]] NativeFileBackend* GetNativeFileBackend(FileSystemDriver& driver) noexcept;
    [[nodiscard]] const NativeFileBackend* GetNativeFileBackend(const FileSystemDriver& driver) noexcept;
}// namespace NGIN::IO::detail
