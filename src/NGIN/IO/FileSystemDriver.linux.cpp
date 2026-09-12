#include "NativeFileSystemBackend.hpp"
#include "RuntimeLoop.hpp"

#if defined(__linux__)

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <linux/io_uring.h>
#include <mutex>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include <vector>

namespace NGIN::IO::detail
{
    namespace
    {
        int IoUringEnter(int ring, unsigned submitted) noexcept
        {
            return static_cast<int>(::syscall(__NR_io_uring_enter, ring, submitted, 0, 0, nullptr, 0));
        }
        unsigned Acquire(unsigned* value) noexcept
        {
            return std::atomic_ref<unsigned>(*value).load(std::memory_order_acquire);
        }
        void Release(unsigned* target, unsigned value) noexcept
        {
            std::atomic_ref<unsigned>(*target).store(value, std::memory_order_release);
        }
    }// namespace

    class IoUringNativeFileBackend final : public NativeFileBackend, public RuntimeLoop::Handler
    {
    public:
        explicit IoUringNativeFileBackend(Runtime& runtime)
            : m_loop(RuntimeAccess::Loop(runtime)), m_requests(runtime.GetOptions().files.queueDepthHint),
              m_batchSize((std::min) (runtime.GetOptions().batchSize, std::size_t {64}))
        {
            m_free.reserve(m_requests.size());
            for (std::size_t index = m_requests.size(); index != 0; --index)
                m_free.push_back(index - 1);
            io_uring_params parameters {};
            m_ring = static_cast<int>(::syscall(__NR_io_uring_setup,
                                                runtime.GetOptions().files.queueDepthHint, &parameters));
            if (m_ring < 0)
                return;
            m_sqSize  = parameters.sq_off.array + parameters.sq_entries * sizeof(unsigned);
            m_cqSize  = parameters.cq_off.cqes + parameters.cq_entries * sizeof(io_uring_cqe);
            m_entries = parameters.sq_entries;
            m_sq      = ::mmap(nullptr, m_sqSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_ring, IORING_OFF_SQ_RING);
            m_cq      = ::mmap(nullptr, m_cqSize, PROT_READ | PROT_WRITE, MAP_SHARED, m_ring, IORING_OFF_CQ_RING);
            m_sqes    = static_cast<io_uring_sqe*>(::mmap(nullptr, m_entries * sizeof(io_uring_sqe),
                                                          PROT_READ | PROT_WRITE, MAP_SHARED, m_ring, IORING_OFF_SQES));
            if (m_sq == MAP_FAILED || m_cq == MAP_FAILED || m_sqes == MAP_FAILED)
                return;
            const auto sqField = [&](unsigned offset) { return reinterpret_cast<unsigned*>(static_cast<char*>(m_sq) + offset); };
            const auto cqField = [&](unsigned offset) { return reinterpret_cast<unsigned*>(static_cast<char*>(m_cq) + offset); };
            m_sqHead           = sqField(parameters.sq_off.head);
            m_sqTail           = sqField(parameters.sq_off.tail);
            m_sqMask           = sqField(parameters.sq_off.ring_mask);
            m_sqArray          = sqField(parameters.sq_off.array);
            m_cqHead           = cqField(parameters.cq_off.head);
            m_cqTail           = cqField(parameters.cq_off.tail);
            m_cqMask           = cqField(parameters.cq_off.ring_mask);
            m_cqes             = reinterpret_cast<io_uring_cqe*>(static_cast<char*>(m_cq) + parameters.cq_off.cqes);
            m_notification     = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
            if (m_notification < 0)
                return;
            if (::syscall(__NR_io_uring_register, m_ring, IORING_REGISTER_EVENTFD, &m_notification, 1) < 0)
                return;
            m_initialized = true;
        }

        ~IoUringNativeFileBackend() override
        {
            if (m_inFlight != 0 || m_identifier != 0)
                std::terminate();
            if (m_ring >= 0)
                ::close(m_ring);
            if (m_notification >= 0)
                ::close(m_notification);
            if (m_sq && m_sq != MAP_FAILED)
                ::munmap(m_sq, m_sqSize);
            if (m_cq && m_cq != MAP_FAILED)
                ::munmap(m_cq, m_cqSize);
            if (m_sqes && m_sqes != MAP_FAILED)
                ::munmap(m_sqes, m_entries * sizeof(io_uring_sqe));
        }

        bool Attach(const std::shared_ptr<IoUringNativeFileBackend>& self) noexcept
        {
            std::lock_guard lock(m_mutex);
            if (!m_initialized)
                return false;
            auto registration = m_loop.Watch(static_cast<std::uintptr_t>(m_notification), RuntimePoller::Read, self);
            if (!registration)
                return false;
            m_identifier = *registration;
            return true;
        }
        FileSystemDriver::ActiveBackend GetActiveBackend() const noexcept override
        {
            return m_initialized ? FileSystemDriver::ActiveBackend::NativeIoUring : FileSystemDriver::ActiveBackend::None;
        }

        bool Submit(NativeFileRequest request, const NGIN::Async::CancellationToken&) noexcept override
        {
            std::lock_guard lock(m_mutex);
            if (!m_initialized || m_stopping || m_free.empty() || !request.completion)
                return false;
            const unsigned head = Acquire(m_sqHead);
            const unsigned tail = Acquire(m_sqTail);
            if (tail - head >= m_entries)
                return false;
            const std::size_t index      = m_free.back();
            auto&             slot       = m_requests[index];
            auto&             submission = m_sqes[tail & *m_sqMask];
            submission                   = {};
            switch (request.kind)
            {
                case NativeFileOperationKind::Read:
                    submission.opcode = IORING_OP_READ;
                    break;
                case NativeFileOperationKind::Write:
                    submission.opcode = IORING_OP_WRITE;
                    break;
                case NativeFileOperationKind::Flush:
                    submission.opcode = IORING_OP_FSYNC;
                    break;
                case NativeFileOperationKind::Close:
                    submission.opcode = IORING_OP_CLOSE;
                    break;
                default:
                    return false;
            }
            // Force potentially blocking file work into the kernel's async
            // context; the runtime thread only admits and observes operations.
            submission.flags = IOSQE_ASYNC;
            submission.fd    = static_cast<int>(request.handleValue);
            if (request.kind == NativeFileOperationKind::Read || request.kind == NativeFileOperationKind::Write)
            {
                submission.addr = reinterpret_cast<__u64>(request.buffer);
                submission.len  = request.size;
                submission.off  = request.useCurrentOffset ? static_cast<__u64>(-1) : request.offset;
            }
            slot.request         = request;
            slot.occupied        = true;
            submission.user_data = static_cast<__u64>(index);
            m_free.pop_back();
            ++m_inFlight;
            m_sqArray[tail & *m_sqMask] = tail & *m_sqMask;
            Release(m_sqTail, tail + 1);
            int submitted;
            do
            {
                submitted = IoUringEnter(m_ring, 1);
            } while (submitted < 0 && errno == EINTR && Acquire(m_sqHead) == head);
            if (submitted <= 0 && Acquire(m_sqHead) == head)
            {
                // This ring has no SQPOLL consumer and submission is serialized.
                // An unconsumed entry after enter returns has not reached the OS.
                Release(m_sqTail, tail);
                slot.occupied = false;
                --m_inFlight;
                m_free.push_back(index);
                return false;
            }
            return true;
        }

        void Ready(const RuntimePoller::Event&) noexcept override { Drain(); }
        void Stop() noexcept override
        {
            {
                std::lock_guard lock(m_mutex);
                m_stopping = true;
            }
            // Blocking file operations may finish naturally. Their common
            // completion reservations keep shutdown alive until final access.
            Drain();
        }

    private:
        struct RequestSlot
        {
            NativeFileRequest request;
            bool              occupied {false};
        };
        void Drain() noexcept
        {
            std::uint64_t notifications;
            while (::read(m_notification, &notifications, sizeof(notifications)) < 0 && errno == EINTR) {}
            for (std::size_t count = 0; count < m_batchSize; ++count)
            {
                NativeFileRequest    request;
                NativeFileCompletion completion;
                {
                    std::lock_guard lock(m_mutex);
                    const unsigned  head = Acquire(m_cqHead);
                    if (head == Acquire(m_cqTail))
                        break;
                    const io_uring_cqe completed = m_cqes[head & *m_cqMask];
                    const std::size_t  index     = static_cast<std::size_t>(completed.user_data);
                    if (index >= m_requests.size() || !m_requests[index].occupied)
                        std::terminate();
                    auto& slot    = m_requests[index];
                    request       = slot.request;
                    slot.occupied = false;
                    --m_inFlight;
                    m_free.push_back(index);
                    Release(m_cqHead, head + 1);
                    completion.status     = NativeFileCompletion::Status::Completed;
                    completion.value      = completed.res < 0 ? -1 : completed.res;
                    completion.systemCode = completed.res < 0 ? -completed.res : 0;
                }
                request.completion(request.userData, completion);
            }
            std::lock_guard lock(m_mutex);
            if (m_stopping && m_inFlight == 0)
            {
                m_loop.Unwatch(m_identifier);
                m_identifier = 0;
            }
            else if (Acquire(m_cqHead) != Acquire(m_cqTail))
            {
                // One eventfd read consumes a coalesced count, not one CQ entry.
                // Reassert readiness when our bounded batch leaves CQ backlog.
                const std::uint64_t one = 1;
                while (::write(m_notification, &one, sizeof(one)) < 0)
                {
                    if (errno == EINTR)
                        continue;
                    if (errno != EAGAIN)
                        std::terminate();
                    break;
                }
            }
        }

        RuntimeLoop&             m_loop;
        std::mutex               m_mutex;
        std::vector<RequestSlot> m_requests;
        std::vector<std::size_t> m_free;
        const std::size_t        m_batchSize;
        int                      m_ring {-1};
        int                      m_notification {-1};
        void*                    m_sq {};
        void*                    m_cq {};
        io_uring_sqe*            m_sqes {};
        io_uring_cqe*            m_cqes {};
        unsigned*                m_sqHead {};
        unsigned*                m_sqTail {};
        unsigned*                m_sqMask {};
        unsigned*                m_sqArray {};
        unsigned*                m_cqHead {};
        unsigned*                m_cqTail {};
        unsigned*                m_cqMask {};
        unsigned                 m_entries {};
        unsigned                 m_inFlight {};
        std::size_t              m_sqSize {};
        std::size_t              m_cqSize {};
        std::uint64_t            m_identifier {};
        bool                     m_initialized {false};
        bool                     m_stopping {false};
    };

    std::shared_ptr<NativeFileBackend> CreateNativeFileBackend(FileSystemDriver& driver)
    {
        auto backend = std::make_shared<IoUringNativeFileBackend>(driver.GetRuntime());
        if (backend->Attach(backend))
            return backend;
        return {};
    }
}// namespace NGIN::IO::detail
#endif
