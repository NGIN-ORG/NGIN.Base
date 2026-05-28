#include <NGIN/Crypto/Memory/ZeroMemory.hpp>

#include <atomic>
#include <cstring>

#if defined(_WIN32)
#include <windows.h>
#endif

namespace NGIN::Crypto::Memory
{
    void SecureZero(void* data, NGIN::UIntSize size) noexcept
    {
        if (data == nullptr || size == 0)
        {
            return;
        }

#if defined(_WIN32)
        SecureZeroMemory(data, size);
#else
        static void* (*const volatile memsetFn)(void*, int, std::size_t) = std::memset;
        memsetFn(data, 0, size);
        std::atomic_signal_fence(std::memory_order_seq_cst);
#endif
    }
}// namespace NGIN::Crypto::Memory
