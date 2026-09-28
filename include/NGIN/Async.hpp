#pragma once

/// @file Async.hpp
/// @brief Umbrella include for asynchronous tasks, generators, cancellation, and completion.

#include <NGIN/Async/AsyncCanceledException.hpp>
#include <NGIN/Async/AsyncConfig.hpp>
#include <NGIN/Async/AsyncDomainErrorException.hpp>
#include <NGIN/Async/AsyncEnvironment.hpp>
#include <NGIN/Async/AsyncExceptionTraits.hpp>
#include <NGIN/Async/AsyncFault.hpp>
#include <NGIN/Async/AsyncError.hpp>
#include <NGIN/Async/AsyncFaultException.hpp>
#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Completion.hpp>
#include <NGIN/Async/CurrentEnvironment.hpp>
#include <NGIN/Async/ExecutorTransitions.hpp>
#include <NGIN/Async/Generator.hpp>
#include <NGIN/Async/NoError.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskCanceled.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Async/TaskScope.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/Race.hpp>

#include <NGIN/Async/ScopeReport.hpp>
#include <NGIN/Async/WithScope.hpp>

#include <NGIN/Async/OperationRef.hpp>
#include <NGIN/Async/FirstCompleted.hpp>

#include <NGIN/Async/WithDeadline.hpp>
#include <NGIN/Async/WithTimeout.hpp>

#include <NGIN/Async/TaskSupervisor.hpp>
