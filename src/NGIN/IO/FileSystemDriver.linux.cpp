#include "NativeFileSystemBackend.hpp"

#if defined(__linux__)

#include <NGIN/Async/AsyncError.hpp>

#include <cerrno>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <linux/io_uring.h>
#include <mutex>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>

namespace NGIN::IO::detail
{
    namespace
    {
        [[nodiscard]] int IoUringSetup(const unsigned entries, io_uring_params* params) noexcept
        {
            return static_cast<int>(::syscall(__NR_io_uring_setup, entries, params));
        }

        [[nodiscard]] int IoUringEnter(const int ringFd, unsigned toSubmit, unsigned minComplete, unsigned flags) noexcept
        {
            return static_cast<int>(::syscall(__NR_io_uring_enter, ringFd, toSubmit, minComplete, flags, nullptr, 0));
        }
    }// namespace

    class IoUringNativeFileBackend final : public NativeFileBackend
    {
    public:
        explicit IoUringNativeFileBackend(const FileSystemDriver::Options& options)
        {
            io_uring_params params {};
            const auto entryCount = options.queueDepthHint == 0 ? 64u : options.queueDepthHint;
            m_ringFd              = IoUringSetup(entryCount, &params);
            if (m_ringFd < 0)
            {
                return;
            }

            const auto sqRingSize = params.sq_off.array + params.sq_entries * sizeof(unsigned);
            const auto cqRingSize = params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe);

            m_sqRing = ::mmap(nullptr, sqRingSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, m_ringFd, IORING_OFF_SQ_RING);
            m_cqRing = ::mmap(nullptr, cqRingSize, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_POPULATE, m_ringFd, IORING_OFF_CQ_RING);
            m_sqes   = static_cast<io_uring_sqe*>(
                    ::mmap(nullptr,
                           params.sq_entries * sizeof(io_uring_sqe),
                           PROT_READ | PROT_WRITE,
                           MAP_SHARED | MAP_POPULATE,
                           m_ringFd,
                           IORING_OFF_SQES));

            if (m_sqRing == MAP_FAILED || m_cqRing == MAP_FAILED || m_sqes == MAP_FAILED)
            {
                CleanupMappings(params);
                return;
            }

            m_sqMask        = reinterpret_cast<unsigned*>(static_cast<char*>(m_sqRing) + params.sq_off.ring_mask);
            m_sqHead        = reinterpret_cast<unsigned*>(static_cast<char*>(m_sqRing) + params.sq_off.head);
            m_sqTail        = reinterpret_cast<unsigned*>(static_cast<char*>(m_sqRing) + params.sq_off.tail);
            m_sqArray       = reinterpret_cast<unsigned*>(static_cast<char*>(m_sqRing) + params.sq_off.array);
            m_cqMask        = reinterpret_cast<unsigned*>(static_cast<char*>(m_cqRing) + params.cq_off.ring_mask);
            m_cqHead        = reinterpret_cast<unsigned*>(static_cast<char*>(m_cqRing) + params.cq_off.head);
            m_cqTail        = reinterpret_cast<unsigned*>(static_cast<char*>(m_cqRing) + params.cq_off.tail);
            m_cqes          = reinterpret_cast<io_uring_cqe*>(static_cast<char*>(m_cqRing) + params.cq_off.cqes);
            m_sqEntryCount  = params.sq_entries;
            m_sqRingSize    = sqRingSize;
            m_cqRingSize    = cqRingSize;
            m_initialized   = true;
            m_worker        = std::thread([this]() noexcept { Run(); });
        }

        ~IoUringNativeFileBackend() override
        {
            {
                std::lock_guard<std::mutex> lock(m_mutex);
                m_stopping = true;
            }
            m_cv.notify_all();
            if (m_worker.joinable())
            {
                m_worker.join();
            }
            CleanupMappings();
        }

        [[nodiscard]] FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept override
        {
            return m_initialized ? FileSystemDriver::ActiveBackend::NativeIoUring : FileSystemDriver::ActiveBackend::None;
        }

        [[nodiscard]] bool Submit(NativeFileRequest request) noexcept override
        {
            if (!m_initialized)
            {
                return false;
            }

            {
                std::lock_guard<std::mutex> lock(m_mutex);
                if (m_stopping)
                {
                    return false;
                }
                m_pending.push_back(new NativeFileRequest(std::move(request)));
            }
            m_cv.notify_one();
            return true;
        }

    private:
        void CleanupMappings(const io_uring_params& params = {}) noexcept
        {
            if (m_sqRing != nullptr && m_sqRing != MAP_FAILED)
            {
                ::munmap(m_sqRing, m_sqRingSize == 0 ? params.sq_off.array + params.sq_entries * sizeof(unsigned) : m_sqRingSize);
            }
            if (m_cqRing != nullptr && m_cqRing != MAP_FAILED)
            {
                ::munmap(m_cqRing, m_cqRingSize == 0 ? params.cq_off.cqes + params.cq_entries * sizeof(io_uring_cqe) : m_cqRingSize);
            }
            if (m_sqes != nullptr && m_sqes != MAP_FAILED)
            {
                ::munmap(m_sqes, (m_sqEntryCount == 0 ? params.sq_entries : m_sqEntryCount) * sizeof(io_uring_sqe));
            }
            if (m_ringFd >= 0)
            {
                ::close(m_ringFd);
            }
            m_sqRing = nullptr;
            m_cqRing = nullptr;
            m_sqes   = nullptr;
            m_ringFd = -1;
        }

        void CompleteFault(NativeFileRequest& request, NGIN::Async::AsyncFaultCode code, const int native = 0) noexcept
        {
            if (request.completion == nullptr)
            {
                return;
            }
            request.completion(
                    request.userData,
                    NativeFileCompletion {
                            .status = NativeFileCompletion::Status::Fault,
                            .fault = NGIN::Async::MakeAsyncFault(code, native),
                    });
        }

        bool PrepareSubmission(const NativeFileRequest& request) noexcept
        {
            const unsigned head = *m_sqHead;
            const unsigned tail = *m_sqTail;
            if (tail - head >= m_sqEntryCount)
            {
                return false;
            }

            auto& sqe = m_sqes[tail & *m_sqMask];
            sqe       = {};
            switch (request.kind)
            {
                case NativeFileOperationKind::Read:
                    sqe.opcode = IORING_OP_READ;
                    sqe.fd     = static_cast<int>(request.handleValue);
                    sqe.addr   = reinterpret_cast<__u64>(request.buffer);
                    sqe.len    = request.size;
                    sqe.off    = request.useCurrentOffset ? static_cast<__u64>(-1) : request.offset;
                    break;
                case NativeFileOperationKind::Write:
                    sqe.opcode = IORING_OP_WRITE;
                    sqe.fd     = static_cast<int>(request.handleValue);
                    sqe.addr   = reinterpret_cast<__u64>(request.buffer);
                    sqe.len    = request.size;
                    sqe.off    = request.useCurrentOffset ? static_cast<__u64>(-1) : request.offset;
                    break;
                case NativeFileOperationKind::Flush:
                    sqe.opcode = IORING_OP_FSYNC;
                    sqe.fd     = static_cast<int>(request.handleValue);
                    break;
                case NativeFileOperationKind::Close:
                    sqe.opcode = IORING_OP_CLOSE;
                    sqe.fd     = static_cast<int>(request.handleValue);
                    break;
            }
            sqe.user_data               = reinterpret_cast<__u64>(&request);
            m_sqArray[tail & *m_sqMask] = tail & *m_sqMask;
            *m_sqTail                   = tail + 1;
            ++m_submittedSinceEnter;
            ++m_inFlight;
            return true;
        }

        void SubmitQueuedRequests() noexcept
        {
            for (;;)
            {
                NativeFileRequest* request = nullptr;
                {
                    std::lock_guard<std::mutex> lock(m_mutex);
                    if (m_pending.empty())
                    {
                        break;
                    }
                    request = m_pending.front();
                    if (!PrepareSubmission(*request))
                    {
                        break;
                    }
                    m_pending.pop_front();
                }
            }

            if (m_submittedSinceEnter > 0)
            {
                if (IoUringEnter(m_ringFd, m_submittedSinceEnter, 0, 0) < 0)
                {
                    const int native = errno;
                    m_submittedSinceEnter = 0;
                    std::lock_guard<std::mutex> lock(m_mutex);
                    while (!m_pending.empty())
                    {
                        auto* request = m_pending.front();
                        m_pending.pop_front();
                        CompleteFault(*request, NGIN::Async::AsyncFaultCode::SchedulerFailure, native);
                        delete request;
                    }
                }
                m_submittedSinceEnter = 0;
            }
        }

        void DrainCompletions() noexcept
        {
            unsigned cqHead = *m_cqHead;
            const unsigned cqTail = *m_cqTail;
            while (cqHead != cqTail)
            {
                auto& cqe = m_cqes[cqHead & *m_cqMask];
                auto* request = reinterpret_cast<NativeFileRequest*>(static_cast<std::uintptr_t>(cqe.user_data));
                if (request != nullptr)
                {
                    if (request->completion != nullptr)
                    {
                        NativeFileCompletion completion;
                        completion.status = NativeFileCompletion::Status::Completed;
                        if (cqe.res < 0)
                        {
                            completion.value      = -1;
                            completion.systemCode = -cqe.res;
                        }
                        else
                        {
                            completion.value = cqe.res;
                        }
                        request->completion(request->userData, completion);
                    }
                    delete request;
                }
                ++cqHead;
                --m_inFlight;
            }
            *m_cqHead = cqHead;
        }

        void Run() noexcept
        {
            for (;;)
            {
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    m_cv.wait(lock, [this]() noexcept { return m_stopping || !m_pending.empty() || m_inFlight > 0; });
                    if (m_stopping && m_pending.empty() && m_inFlight == 0)
                    {
                        return;
                    }
                }

                SubmitQueuedRequests();
                if (m_inFlight > 0)
                {
                    (void) IoUringEnter(m_ringFd, 0, 1, IORING_ENTER_GETEVENTS);
                    DrainCompletions();
                }
            }
        }

        int                     m_ringFd {-1};
        void*                   m_sqRing {nullptr};
        void*                   m_cqRing {nullptr};
        io_uring_sqe*           m_sqes {nullptr};
        unsigned*               m_sqHead {nullptr};
        unsigned*               m_sqTail {nullptr};
        unsigned*               m_sqMask {nullptr};
        unsigned*               m_sqArray {nullptr};
        unsigned*               m_cqHead {nullptr};
        unsigned*               m_cqTail {nullptr};
        unsigned*               m_cqMask {nullptr};
        io_uring_cqe*           m_cqes {nullptr};
        unsigned                m_sqEntryCount {0};
        std::size_t             m_sqRingSize {0};
        std::size_t             m_cqRingSize {0};
        bool                    m_initialized {false};
        bool                    m_stopping {false};
        unsigned                m_inFlight {0};
        unsigned                m_submittedSinceEnter {0};
        std::mutex              m_mutex {};
        std::condition_variable m_cv {};
        std::deque<NativeFileRequest*> m_pending {};
        std::thread             m_worker {};
    };

    std::unique_ptr<NativeFileBackend> CreateNativeFileBackend(const FileSystemDriver::Options& options)
    {
        auto backend = std::make_unique<IoUringNativeFileBackend>(options);
        if (backend->GetActiveBackend() != FileSystemDriver::ActiveBackend::None)
        {
            return backend;
        }
        return {};
    }
}// namespace NGIN::IO::detail

#endif
