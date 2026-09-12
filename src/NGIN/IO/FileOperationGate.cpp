#include "FileOperationGate.hpp"
#include "FileSystemDriver.hpp"

#include <cassert>
#include <limits>

namespace NGIN::IO::detail
{
    ResultVoid ValidateFileTransfer(const Path& path, bool allowed, bool writing, bool append,
                                    UIntSize size, std::optional<UInt64> offset)
    {
        IOError error;
        if (!allowed)
        {
            error.code    = IOErrorCode::NotSupported;
            error.message = writing ? "file not opened for write" : "file not opened for read";
        }
        else if (writing && append && offset)
        {
            error.code    = IOErrorCode::NotSupported;
            error.message = "explicit-offset writes are not supported on append handles";
        }
        else if (size > std::numeric_limits<UInt32>::max())
        {
            error.code    = IOErrorCode::InvalidArgument;
            error.message = "file transfer exceeds the 32-bit backend length limit; split the transfer";
        }
        else if (offset && *offset > static_cast<UInt64>(std::numeric_limits<Int64>::max()))
        {
            error.code    = IOErrorCode::InvalidArgument;
            error.message = "file offset exceeds the signed 64-bit backend limit";
        }
        else
            return {};
        error.path = path;
        return NGIN::Utilities::Unexpected<IOError>(std::move(error));
    }

    FileOperationGate::~FileOperationGate()
    {
        assert(m_active == 0 && m_head == nullptr);
    }

    bool FileOperationGate::IsClosing() const noexcept
    {
        std::lock_guard lock(m_mutex);
        return m_closing;
    }

    void FileOperationGate::Admission::Reset(bool closed) noexcept
    {
        m_node.registration.Reset();
        if (std::exchange(m_node.admitted, false))
            m_node.gate->m_driver.ReleaseHandleOperation();
        if (std::exchange(m_node.granted, false))
            m_node.gate->Release(m_node.kind, closed);
    }

    void FileOperationGate::Remove(Node& node) noexcept
    {
        if (node.previous)
            node.previous->next = node.next;
        else
            m_head = node.next;
        if (node.next)
            node.next->previous = node.previous;
        else
            m_tail = node.previous;
        node.previous = node.next = nullptr;
    }

    FileOperationGate::Node* FileOperationGate::Activate(Node* immediate) noexcept
    {
        Node*  ready {};
        Node** tail = &ready;
        if (m_barrierActive)
            return ready;
        for (Node* node = m_head; node;)
        {
            Node*      next    = node->next;
            const bool barrier = node->kind == Kind::Flush || node->kind == Kind::Close;
            if (barrier && (m_active != 0 || node != m_head))
                break;
            if (node->kind == Kind::Position && m_positionActive)
            {
                node = next;
                continue;
            }
            Remove(*node);
            node->phase   = Phase::Active;
            node->granted = true;
            ++m_active;
            m_positionActive |= node->kind == Kind::Position;
            m_barrierActive = barrier;
            if (node != immediate)
            {
                *tail = node;
                tail  = &node->readyNext;
            }
            if (barrier)
                break;
            node = next;
        }
        return ready;
    }

    void FileOperationGate::Dispatch(Node* ready) noexcept
    {
        while (ready)
        {
            Node* next         = ready->readyNext;
            auto* target       = ready->target;
            auto  continuation = std::move(ready->continuation);
            target->QueueContinuation(std::move(continuation));
            ready = next;
        }
    }

    bool FileOperationGate::Enqueue(Node& node) noexcept
    {
        Node* ready;
        bool  pending;
        {
            std::lock_guard lock(m_mutex);
            if (node.canceled || m_closed || m_closing)
            {
                node.phase  = Phase::Done;
                node.status = node.canceled ? Status::Canceled : m_closed ? Status::Closed
                                                                          : Status::Closing;
                return false;
            }
            const auto capacity = m_driver.AcquireHandleOperation();
            if (!capacity)
            {
                node.phase  = Phase::Done;
                node.status = capacity.error() == NGIN::Execution::ScheduleError::Stopped ? Status::Canceled : Status::Fault;
                node.fault  = NGIN::Async::detail::MakeSchedulingFault(
                        NGIN::Async::AsyncFaultCode::SchedulerDispatchFailed, capacity.error());
                return false;
            }
            node.admitted = true;
            if (node.kind == Kind::Close)
                m_closing = true;
            node.phase    = Phase::Queued;
            node.previous = m_tail;
            if (m_tail)
                m_tail->next = &node;
            else
                m_head = &node;
            m_tail  = &node;
            ready   = Activate(&node);
            pending = node.phase == Phase::Queued;
        }
        Dispatch(ready);
        return pending;
    }

    void FileOperationGate::Cancel(Node& node) noexcept
    {
        Node* ready {};
        {
            std::lock_guard lock(m_mutex);
            node.canceled = true;
            if (node.phase == Phase::Queued)
            {
                Remove(node);
                if (node.kind == Kind::Close)
                    m_closing = false;
                node.phase     = Phase::Done;
                node.status    = Status::Canceled;
                node.readyNext = Activate();
                ready          = &node;
            }
        }
        Dispatch(ready);
    }

    void FileOperationGate::Release(Kind kind, bool closed) noexcept
    {
        Node* ready;
        {
            std::lock_guard lock(m_mutex);
            assert(m_active != 0);
            --m_active;
            if (kind == Kind::Position)
                m_positionActive = false;
            if (kind == Kind::Flush || kind == Kind::Close)
                m_barrierActive = false;
            if (kind == Kind::Close)
            {
                m_closed  = closed;
                m_closing = closed;
            }
            ready = Activate();
        }
        Dispatch(ready);
    }
}// namespace NGIN::IO::detail
