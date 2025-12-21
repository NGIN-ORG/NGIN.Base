#pragma once

namespace NGIN::Sync
{
    class ILockable
    {
    public:
        virtual ~ILockable()                          = default;
        virtual void Lock()                           = 0;
        virtual void Unlock()                         = 0;
        [[nodiscard]] virtual bool TryLock() noexcept = 0;
    };
}// namespace NGIN::Sync
