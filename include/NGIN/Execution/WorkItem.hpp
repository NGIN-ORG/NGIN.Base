/// @file WorkItem.hpp
/// @brief A schedulable work item: coroutine continuation or job.
#pragma once

#include <cstddef>
#include <concepts>
#include <coroutine>
#include <exception>
#include <memory>
#include <new>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <NGIN/Utilities/Callable.hpp>

namespace NGIN::Execution
{
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
                    auto* ptr = new T(std::forward<F>(job));
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
                                delete ptr;
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
