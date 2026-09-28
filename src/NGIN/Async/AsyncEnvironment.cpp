#include <NGIN/Async/AsyncEnvironment.hpp>

#include <stdexcept>

namespace NGIN::Async
{
    void AsyncEnvironment::Validate() const
    {
        if (!resource)
            throw std::invalid_argument("AsyncEnvironment requires a non-null allocation resource");
    }
}// namespace NGIN::Async
