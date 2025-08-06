#pragma once

#include <cstddef>
#include <vector>
#include <stdexcept>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>

namespace NGIN::Crypto::Random
{

    inline void GetBytes(void* out, size_t len)
    {
        if (len == 0)
            return;
        int fd = open("/dev/urandom", O_RDONLY);
        if (fd < 0)
            throw std::runtime_error("/dev/urandom open failed");
        size_t total = 0;
        while (total < len)
        {
            ssize_t got = read(fd, static_cast<char*>(out) + total, len - total);
            if (got <= 0)
            {
                close(fd);
                throw std::runtime_error("/dev/urandom read failed");
            }
            total += static_cast<size_t>(got);
        }
        close(fd);
    }

    inline std::vector<uint8_t> GetBytes(size_t len)
    {
        std::vector<uint8_t> buf(len);
        GetBytes(buf.data(), len);
        return buf;
    }

}// namespace NGIN::Crypto::Random
