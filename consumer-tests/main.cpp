#include <NGIN/BaseVersion.hpp>
#include <NGIN/Timer.hpp>

int main()
{
    NGIN::Timer timer;
    timer.Start();
    timer.Stop();
    return NGIN::BaseVersion::Major == 0 && !timer.IsRunning() ? 0 : 1;
}
