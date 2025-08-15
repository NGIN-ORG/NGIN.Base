#include <memory>
#include <atomic>
namespace NGIN::Async
{
    // Cancellation primitives
    class CancellationToken {
    public:
        CancellationToken() = default;
        explicit CancellationToken(std::shared_ptr<std::atomic_bool> flag)
            : m_flag(std::move(flag)) {}
        bool IsCancellationRequested() const {
            return m_flag && m_flag->load();
        }
        operator bool() const { return IsCancellationRequested(); }
    private:
        std::shared_ptr<std::atomic_bool> m_flag;
        friend class CancellationSource;
    };

    class CancellationSource {
    public:
        CancellationSource()
            : m_flag(std::make_shared<std::atomic_bool>(false)) {}
        void Cancel() { m_flag->store(true); }
        CancellationToken GetToken() const { return CancellationToken(m_flag); }
        bool IsCancellationRequested() const { return m_flag->load(); }
    private:
        std::shared_ptr<std::atomic_bool> m_flag;
    };
}