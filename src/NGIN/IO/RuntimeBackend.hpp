#pragma once

#include <NGIN/IO/Runtime.hpp>
#include <memory>

namespace NGIN::IO
{
    namespace detail
    {
        class FileSystemDriver;
        // The IO component owns lifecycle without linking against the Net component.
        // Net supplies this private implementation when a bound socket first awaits I/O.
        class NetworkBackend
        {
        public:
            virtual ~NetworkBackend() = default;
            virtual void Run()        = 0;
            virtual void PollOnce()   = 0;
            virtual void Stop()       = 0;
        };

        struct RuntimeAccess
        {
            using NetworkFactory = std::shared_ptr<NetworkBackend> (*)(const Runtime::NetworkOptions&);
            static NGIN_IO_API std::shared_ptr<FileSystemDriver> Files(Runtime& runtime);
            static NGIN_IO_API std::shared_ptr<NetworkBackend> Network(Runtime& runtime, NetworkFactory factory);
        };
    }// namespace detail
}// namespace NGIN::IO
