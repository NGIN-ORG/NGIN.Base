#pragma once

/// @file Execution.hpp
/// @brief Convenience surface for asynchronous work, schedulers, threads, and fibers.

#include <NGIN/Async/AsyncGenerator.hpp>
#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Generator.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Async/TaskContext.hpp>
#include <NGIN/Async/WhenAll.hpp>
#include <NGIN/Async/WhenAny.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/Fiber.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ThisFiber.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadName.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
