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
    using AsyncResult = Result<T>;

    template<typename T>
    using AsyncTask = NGIN::Async::Task<T, IOError>;

    using AsyncTaskVoid = NGIN::Async::Task<void, IOError>;

    template<typename T>
    [[nodiscard]] inline AsyncResult<T> ToAsyncResult(Result<T>&& result)
    {
        if (!result.HasValue())
            return AsyncResult<T>(NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError()));
        return AsyncResult<T>(std::move(result).TakeValue());
    }

    [[nodiscard]] inline AsyncResult<void> ToAsyncResult(ResultVoid&& result)
    {
        if (!result.HasValue())
            return AsyncResult<void>(NGIN::Utilities::Unexpected<IOError>(std::move(result).TakeError()));
        return AsyncResult<void> {};
    }
}// namespace NGIN::IO
