// Platform-specific implementation for Windows
#pragma once

// Add your Windows-specific code here

namespace NGIN::Async {
class Thread {
public:
    Thread() noexcept = default;
    explicit Thread(std::function<void()> func)
        : m_thread(std::move(func)) {}
    ~Thread()
    {
        if (m_thread.joinable())
            std::terminate();
    }

    void Start(std::function<void()> func)
    {
        if (m_thread.joinable())
            throw std::logic_error("Thread already started");
        m_thread = std::thread(std::move(func));
    }

    void Join()
    {
        if (m_thread.joinable())
            m_thread.join();
    }

    void Detach()
    {
        if (m_thread.joinable())
            m_thread.detach();
    }

    [[nodiscard]] bool IsJoinable() const noexcept
    {
        return m_thread.joinable();
    }

    void SetName(const std::string& name)
    {
        auto h = m_thread.native_handle();
        std::wstring wname(name.begin(), name.end());
        SetThreadDescription(h, wname.c_str());
    }

    [[nodiscard]] std::thread::id GetId() const noexcept
    {
        return m_thread.get_id();
    }

    template<typename Rep, typename Period>
    static void SleepFor(const std::chrono::duration<Rep, Period>& d)
    {
        std::this_thread::sleep_for(d);
    }

    template<typename Clock, typename Duration>
    static void SleepUntil(const std::chrono::time_point<Clock, Duration>& tp)
    {
        std::this_thread::sleep_until(tp);
    }

private:
    std::thread m_thread;
};
} // namespace NGIN::Async