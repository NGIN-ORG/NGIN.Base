/// @file WorkItem.hpp
/// @brief A schedulable work item: coroutine continuation or job.
#pragma once

#include <cstddef>
#include <concepts>
#include <coroutine>
#include <exception>
#include <atomic>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <NGIN/Utilities/Callable.hpp>

namespace NGIN::Execution
{
    namespace detail
    {
        class JobPool final
        {
        public:
            JobPool()                          = delete;
            JobPool(const JobPool&)            = delete;
            JobPool& operator=(const JobPool&) = delete;
            JobPool(JobPool&&)                 = delete;
            JobPool& operator=(JobPool&&)      = delete;
            ~JobPool()                         = delete;

            static constexpr std::size_t PoolAlignment = alignof(std::max_align_t);
            static constexpr std::size_t Class64       = 64;
            static constexpr std::size_t Class128      = 128;
            static constexpr std::size_t Class256      = 256;
            static constexpr std::size_t Class512      = 512;

            static void* Allocate(std::size_t size, std::size_t alignment)
            {
                if (alignment > PoolAlignment)
                {
                    return ::operator new(size, std::align_val_t(alignment));
                }

                const auto classSize = SizeClass(size);
                if (classSize == 0)
                {
                    return ::operator new(size, std::align_val_t(PoolAlignment));
                }

                auto& head = HeadFor(classSize);
                if (auto* node = Pop(head))
                {
                    return node;
                }

                Refill(classSize);

                if (auto* node = Pop(head))
                {
                    return node;
                }

                return ::operator new(size, std::align_val_t(PoolAlignment));
            }

            static void Deallocate(void* ptr, std::size_t size, std::size_t alignment) noexcept
            {
                if (!ptr)
                {
                    return;
                }

                if (alignment > PoolAlignment)
                {
                    ::operator delete(ptr, std::align_val_t(alignment));
                    return;
                }

                const auto classSize = SizeClass(size);
                if (classSize == 0)
                {
                    ::operator delete(ptr, std::align_val_t(PoolAlignment));
                    return;
                }

                Push(HeadFor(classSize), static_cast<Node*>(ptr));
            }

        private:
            struct Node final
            {
                Node* next {nullptr};
            };

            static constexpr std::size_t SizeClass(std::size_t size) noexcept
            {
                if (size == 0)
                {
                    return 0;
                }
                if (size <= Class64)
                {
                    return Class64;
                }
                if (size <= Class128)
                {
                    return Class128;
                }
                if (size <= Class256)
                {
                    return Class256;
                }
                if (size <= Class512)
                {
                    return Class512;
                }
                return 0;
            }

            static std::atomic<Node*>& HeadFor(std::size_t classSize) noexcept
            {
                switch (classSize)
                {
                    case Class64: return s_head64;
                    case Class128: return s_head128;
                    case Class256: return s_head256;
                    default: return s_head512;
                }
            }

            static Node* Pop(std::atomic<Node*>& head) noexcept
            {
                Node* node = head.load(std::memory_order_acquire);
                while (node)
                {
                    Node* next = node->next;
                    if (head.compare_exchange_weak(node, next, std::memory_order_acq_rel, std::memory_order_acquire))
                    {
                        node->next = nullptr;
                        return node;
                    }
                }
                return nullptr;
            }

            static void Push(std::atomic<Node*>& head, Node* node) noexcept
            {
                Node* cur = head.load(std::memory_order_relaxed);
                do
                {
                    node->next = cur;
                } while (!head.compare_exchange_weak(cur, node, std::memory_order_release, std::memory_order_relaxed));
            }

            static void Refill(std::size_t classSize)
            {
                static constexpr std::size_t blocksPerSlab = 64;
                const auto slabBytes = classSize * blocksPerSlab;
                auto* slab = static_cast<std::byte*>(::operator new(slabBytes, std::align_val_t(PoolAlignment)));

                auto& head = HeadFor(classSize);
                for (std::size_t i = 0; i < blocksPerSlab; ++i)
                {
                    auto* node = std::launder(reinterpret_cast<Node*>(slab + i * classSize));
                    Push(head, node);
                }
            }

            inline static std::atomic<Node*> s_head64 {nullptr};
            inline static std::atomic<Node*> s_head128 {nullptr};
            inline static std::atomic<Node*> s_head256 {nullptr};
            inline static std::atomic<Node*> s_head512 {nullptr};
        };
    }// namespace detail

    /// @brief A move-only unit of work that can be executed by an executor/scheduler.
    ///
    /// WorkItem is a lightweight wrapper that can represent either:
    /// - a `std::coroutine_handle<>` continuation, or
    /// - a normal job (`NGIN::Utilities::Callable<void()>`).
    ///
    /// @note `Invoke()` is `noexcept`; any exception escaping the job/coroutine will call `std::terminate()`.
    class WorkItem final
    {
    public:
        enum class Kind : unsigned char
        {
            None,
            Coroutine,
            Job,
        };

        constexpr WorkItem() noexcept = default;

        explicit WorkItem(std::coroutine_handle<> coroutine) noexcept
            : m_kind(Kind::Coroutine)
        {
            m_storage.coroutine = coroutine;
        }

        explicit WorkItem(NGIN::Utilities::Callable<void()> job)
            : m_kind(Kind::Job)
        {
            if (!job)
            {
                throw std::invalid_argument("NGIN::Execution::WorkItem: job must be non-empty");
            }
            new (&m_storage.job) JobStorage();
            m_storage.job.Init(std::move(job));
        }

        template<typename F>
            requires(!std::is_same_v<std::remove_cvref_t<F>, WorkItem>) &&
                    (!std::is_same_v<std::remove_cvref_t<F>, NGIN::Utilities::Callable<void()>>) &&
                    std::invocable<std::remove_reference_t<F>&> &&
                    std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
        explicit WorkItem(F&& job)
            : m_kind(Kind::Job)
        {
            new (&m_storage.job) JobStorage();
            m_storage.job.Init(std::forward<F>(job));
        }

        WorkItem(WorkItem&& other) noexcept
        {
            MoveFrom(std::move(other));
        }

        WorkItem& operator=(WorkItem&& other) noexcept
        {
            if (this != &other)
            {
                Reset();
                MoveFrom(std::move(other));
            }
            return *this;
        }

        WorkItem(const WorkItem&)            = delete;
        WorkItem& operator=(const WorkItem&) = delete;

        ~WorkItem()
        {
            Reset();
        }

        [[nodiscard]] constexpr Kind GetKind() const noexcept
        {
            return m_kind;
        }

        [[nodiscard]] constexpr bool IsEmpty() const noexcept
        {
            return m_kind == Kind::None;
        }

        [[nodiscard]] constexpr bool IsCoroutine() const noexcept
        {
            return m_kind == Kind::Coroutine;
        }

        [[nodiscard]] constexpr bool IsJob() const noexcept
        {
            return m_kind == Kind::Job;
        }

        [[nodiscard]] std::coroutine_handle<> GetCoroutine() const noexcept
        {
            return (m_kind == Kind::Coroutine) ? m_storage.coroutine : std::coroutine_handle<> {};
        }

        void Invoke() noexcept
        {
            try
            {
                if (m_kind == Kind::Coroutine)
                {
                    if (m_storage.coroutine && !m_storage.coroutine.done())
                    {
                        m_storage.coroutine.resume();
                    }
                    return;
                }
                if (m_kind == Kind::Job)
                {
                    m_storage.job.Invoke();
                    return;
                }
            } catch (...)
            {
                std::terminate();
            }
        }

    private:
        class JobStorage final
        {
        public:
            JobStorage() noexcept = default;

            JobStorage(const JobStorage&)            = delete;
            JobStorage& operator=(const JobStorage&) = delete;

            JobStorage(JobStorage&& other) noexcept
            {
                MoveFrom(std::move(other));
            }

            JobStorage& operator=(JobStorage&& other) noexcept
            {
                if (this != &other)
                {
                    Reset();
                    MoveFrom(std::move(other));
                }
                return *this;
            }

            ~JobStorage()
            {
                Reset();
            }

            template<typename F>
                requires std::invocable<std::remove_reference_t<F>&> &&
                         std::same_as<std::invoke_result_t<std::remove_reference_t<F>&>, void>
            void Init(F&& job)
            {
                using T = std::remove_cvref_t<F>;

                constexpr bool canInline = sizeof(T) <= BufferSize && alignof(T) <= BufferAlignment &&
                                           std::is_nothrow_move_constructible_v<T>;
                if constexpr (canInline)
                {
                    new (StoragePtr()) T(std::forward<F>(job));
                    m_vtable = &GetVTable<T, false>();
                }
                else
                {
                    void* mem = detail::JobPool::Allocate(sizeof(T), alignof(T));
                    auto* ptr = new (mem) T(std::forward<F>(job));
                    *static_cast<T**>(StoragePtr()) = ptr;
                    m_vtable = &GetVTable<T, true>();
                }
            }

            void Invoke() noexcept
            {
                if (m_vtable)
                {
                    m_vtable->invoke(StoragePtr());
                }
            }

            void Reset() noexcept
            {
                if (m_vtable)
                {
                    m_vtable->destroy(StoragePtr());
                    m_vtable = nullptr;
                }
            }

            void MoveFrom(JobStorage&& other) noexcept
            {
                m_vtable = other.m_vtable;
                if (m_vtable)
                {
                    m_vtable->move(StoragePtr(), other.StoragePtr());
                    other.m_vtable = nullptr;
                }
            }

        private:
            using InvokeFn  = void (*)(void*) noexcept;
            using DestroyFn = void (*)(void*) noexcept;
            using MoveFn    = void (*)(void* dest, void* src) noexcept;

            struct VTable final
            {
                InvokeFn  invoke;
                DestroyFn destroy;
                MoveFn    move;
            };

            static constexpr std::size_t BufferSize      = sizeof(void*) * 6;
            static constexpr std::size_t BufferAlignment = alignof(std::max_align_t);

            template<typename T, bool Heap>
            static const VTable& GetVTable() noexcept
            {
                static const VTable vtable {
                        +[](void* storage) noexcept {
                            if constexpr (Heap)
                            {
                                auto* ptr = *static_cast<T**>(storage);
                                (*ptr)();
                            }
                            else
                            {
                                auto* obj = static_cast<T*>(storage);
                                (*obj)();
                            }
                        },
                        +[](void* storage) noexcept {
                            if constexpr (Heap)
                            {
                                auto* ptr = *static_cast<T**>(storage);
                                std::destroy_at(ptr);
                                detail::JobPool::Deallocate(ptr, sizeof(T), alignof(T));
                            }
                            else
                            {
                                std::destroy_at(static_cast<T*>(storage));
                            }
                        },
                        +[](void* dest, void* src) noexcept {
                            if constexpr (Heap)
                            {
                                auto*& destPtr = *static_cast<T**>(dest);
                                auto*& srcPtr  = *static_cast<T**>(src);
                                destPtr        = srcPtr;
                                srcPtr         = nullptr;
                            }
                            else
                            {
                                auto* srcObj = static_cast<T*>(src);
                                new (dest) T(std::move(*srcObj));
                                std::destroy_at(srcObj);
                            }
                        },
                };

                return vtable;
            }

            void* StoragePtr() noexcept
            {
                return m_storage;
            }

            const void* StoragePtr() const noexcept
            {
                return m_storage;
            }

            const VTable* m_vtable {nullptr};
            alignas(BufferAlignment) std::byte m_storage[BufferSize] {};
        };

        union Storage
        {
            std::coroutine_handle<> coroutine;
            JobStorage job;

            constexpr Storage() noexcept
                : coroutine(nullptr)
            {
            }

            ~Storage() {}
        };

        void Reset() noexcept
        {
            if (m_kind == Kind::Job)
            {
                std::destroy_at(std::addressof(m_storage.job));
            }
            m_kind             = Kind::None;
            m_storage.coroutine = nullptr;
        }

        void MoveFrom(WorkItem&& other) noexcept
        {
            m_kind = other.m_kind;
            if (m_kind == Kind::Coroutine)
            {
                m_storage.coroutine = other.m_storage.coroutine;
                other.m_storage.coroutine = nullptr;
                other.m_kind              = Kind::None;
                return;
            }
            if (m_kind == Kind::Job)
            {
                new (&m_storage.job) JobStorage(std::move(other.m_storage.job));
                std::destroy_at(std::addressof(other.m_storage.job));
                other.m_kind = Kind::None;
                other.m_storage.coroutine = nullptr;
                return;
            }
        }

        Kind m_kind {Kind::None};
        Storage m_storage {};
    };
}// namespace NGIN::Execution
