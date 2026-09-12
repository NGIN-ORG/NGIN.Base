#pragma once

/// @file Execution.hpp
/// @brief Umbrella include for executors, schedulers, threads, and fibers.

#include <NGIN/Execution/CompletionReservation.hpp>
#include <NGIN/Execution/Concepts.hpp>
#include <NGIN/Execution/Config.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <NGIN/Execution/ExecutorRef.hpp>
#include <NGIN/Execution/Fiber.hpp>
#include <NGIN/Execution/FiberScheduler.hpp>
#include <NGIN/Execution/InlineScheduler.hpp>
#include <NGIN/Execution/ScheduleResult.hpp>
#include <NGIN/Execution/ThisFiber.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Execution/ThreadName.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/Execution/TimerRegistration.hpp>
#include <NGIN/Execution/WorkItem.hpp>
