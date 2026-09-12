#include <NGIN/IO/RuntimeRunner.hpp>

#include "RuntimeBackend.hpp"

#include <exception>
#include <future>
#include <stdexcept>
#include <thread>

namespace NGIN::IO
{
    struct RuntimeRunner::Impl
    {
        explicit Impl(Runtime& borrowed) : runtime(borrowed)
        {
            auto ready   = std::make_shared<std::promise<void>>();
            auto started = ready->get_future();
            thread       = std::thread([this, ready] {
                bool entered = false;
                try
                {
                    detail::RuntimeAccess::Run(runtime, NGIN::Execution::WorkItem([&entered, ready] {
                                                   entered = true;
                                                   ready->set_value();
                                               }));
                } catch (...)
                {
                    failure = std::current_exception();
                    if (!entered)
                        ready->set_exception(failure);
                }
            });
            try
            {
                started.get();
            } catch (...)
            {
                thread.join();
                throw;
            }
        }

        Runtime&           runtime;
        std::thread        thread;
        std::exception_ptr failure;
    };

    RuntimeRunner::RuntimeRunner(Runtime& runtime) : m_impl(std::make_unique<Impl>(runtime)) {}
    RuntimeRunner::~RuntimeRunner()
    {
        try
        {
            Shutdown();
        } catch (...)
        {
            std::terminate();
        }
    }
    void RuntimeRunner::Shutdown()
    {
        if (!m_impl->thread.joinable())
            return;
        if (m_impl->thread.get_id() == std::this_thread::get_id())
            throw std::logic_error("RuntimeRunner::Shutdown cannot join its callback thread; use Runtime::RequestStop");
        m_impl->runtime.RequestStop();
        m_impl->thread.join();
        if (m_impl->failure)
            std::rethrow_exception(m_impl->failure);
    }
}// namespace NGIN::IO
