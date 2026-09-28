#include <NGIN/Async/TaskSupervisor.hpp>
#include <NGIN/Execution/CooperativeScheduler.hpp>
#include <cstdlib>
#include <exception>
#include <string_view>

namespace
{
    void Terminated() noexcept { std::_Exit(86); }
    NGIN::Async::Task<void> Child() { co_return; }
}

int main(int argc, char** argv)
{
    if (argc != 2)
        return 2;
    NGIN::Execution::CooperativeScheduler executor;
    NGIN::Async::TaskContext context(executor);
    const std::string_view mode(argv[1]);
    if (mode == "supervisor")
    {
        NGIN::Async::TaskSupervisor<> owner(context.GetEnvironment(), 1);
        if (!owner.Spawn(Child()))
            return 3;
        std::set_terminate(Terminated);
        return 0; // Live child destruction must terminate even with NDEBUG.
    }
    NGIN::Async::TaskScope<> scope(context.GetEnvironment(), 1);
    if (!scope.Spawn(Child()))
        return 3;
    if (mode == "live")
    {
        std::set_terminate(Terminated);
        return 0;
    }
    executor.RunUntilIdle();
    auto join = scope.Join();
    if (!join.await_ready())
        return 4;
    std::set_terminate(Terminated);
    if (mode == "joined")
        return join.await_resume().Succeeded() ? 0 : 5;
    if (mode == "joining")
        return 0; // Destruction while Join is active must terminate.
    return 2;
}
