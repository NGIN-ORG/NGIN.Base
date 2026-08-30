#pragma once

#include <NGIN/Async/Task.hpp>
#include <NGIN/IO/IOError.hpp>
#include <NGIN/Utilities/Expected.hpp>

namespace NGIN::IO
{
    template<typename T>
    using Result = NGIN::Utilities::Expected<T, IOError>;

    using ResultVoid = NGIN::Utilities::Expected<void, IOError>;

    template<typename T>
    using AsyncTask = NGIN::Async::Task<T, IOError>;

    using AsyncTaskVoid = NGIN::Async::Task<void, IOError>;
}// namespace NGIN::IO
