#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/Fiber.hpp>
#include <NGIN/Execution/ThisThread.hpp>
#include <NGIN/Execution/Thread.hpp>
#include <NGIN/Sync/AtomicCondition.hpp>

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Execution public headers compile together")
{
    SUCCEED();
}
