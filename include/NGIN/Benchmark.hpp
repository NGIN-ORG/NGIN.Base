#pragma once

#include <NGIN/Defines.hpp>
#include <NGIN/Primitives.hpp>
#include <algorithm>
#include <cassert>
#include <cmath>
#include <exception>
#include <limits>
#include <mutex>
#include <memory>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <vector>

#include <NGIN/Timer.hpp>
#include <NGIN/Units.hpp>
#include <NGIN/Utilities/Callable.hpp>// SBO‐optimized Callable

namespace NGIN
{
    /// \brief  Passed to your benchmarked function so it can bracket
    ///         exactly the code you want measured.
    class BenchmarkContext
    {
    public:
        BenchmarkContext()
        {
            m_timer.Reset();
        }

        /// \brief  Begin timing.
        void start()
        {
            m_timer.Start();
        }

        /// \brief  End timing and return the elapsed nanoseconds.
        F64 stop()
        {
            m_timer.Stop();
            return m_timer.GetElapsed<Nanoseconds>().GetValue();
        }

        /// \brief  Prevents compiler from optimizing away `value`.
        template<typename T>
        NGIN_ALWAYS_INLINE void doNotOptimize(T const& value) const
        {
#if defined(__clang__) || defined(__GNUC__)
            asm volatile("" : : "g"(value) : "memory");
#elif defined(_MSC_VER)
            _ReadWriteBarrier();
            (void) value;
#else
            volatile char dummy = *reinterpret_cast<char const volatile*>(&value);
            (void) dummy;
#endif
        }

        /// \brief  Prevents compiler from reordering memory around this point.
        NGIN_ALWAYS_INLINE void clobberMemory() const
        {
#if defined(__clang__) || defined(__GNUC__)
            asm volatile("" : : : "memory");
#elif defined(_MSC_VER)
            _ReadWriteBarrier();
#else
            volatile int dummy = 0;
            (void) dummy;
#endif
        }

    private:
        Timer m_timer;
    };

    /// \brief  Configuration parameters for a benchmark run.
    struct BenchmarkConfig
    {
        Int32 iterations        = 1000;
        Int32 warmupIterations  = 100;
        bool accountForOverhead = false;
        bool keepRawTimings     = false;
    };

    template<typename DesiredUnit>
        requires IsUnitOf<DesiredUnit, Time>
    struct BenchmarkResult
    {
        std::string name              = "Unknown Benchmark";
        Int32 numIterations           = 0;
        DesiredUnit averageTime       = DesiredUnit(0.0);
        DesiredUnit minTime           = DesiredUnit(0.0);
        DesiredUnit maxTime           = DesiredUnit(0.0);
        DesiredUnit standardDeviation = DesiredUnit(0.0);
        DesiredUnit medianTime        = DesiredUnit(0.0);
        DesiredUnit percentile25      = DesiredUnit(0.0);
        DesiredUnit percentile75      = DesiredUnit(0.0);
    };

    /// \brief  A simple benchmarking engine that runs a user‐provided
    ///         void(BenchmarkContext&) functor multiple times and gathers stats.
    class Benchmark
    {
    public:
        //------------------------------------------------------------------------
        // Public API
        //------------------------------------------------------------------------

        /// \brief  Construct an empty Benchmark (no callable). This registers
        ///         itself so RunAll() can discover it, but Run() will assert.
        Benchmark(std::string_view benchName = "Unnamed Benchmark")
            : config(defaultConfig), name(benchName)
        {
            // Registration happens in the static Register() methods instead.
        }

        /// \brief  Construct from a callable of signature void(BenchmarkContext&),
        ///         using defaultConfig.
        template<typename F>
            requires std::is_invocable_r_v<void, F, BenchmarkContext&>
        Benchmark(F func, std::string_view benchName = "Unnamed Benchmark")
            : config(defaultConfig), name(benchName)
        {
            m_callable = std::move(func);
        }

        /// \brief  Construct from custom config + callable of signature void(BenchmarkContext&).
        template<typename F>
            requires std::is_invocable_r_v<void, F, BenchmarkContext&>
        Benchmark(const BenchmarkConfig& cfg, F func, std::string_view benchName = "Unnamed Benchmark")
            : config(cfg), name(benchName)
        {
            m_callable = std::move(func);
        }

        // noncopyable, nonmovable
        Benchmark(const Benchmark&)                = delete;
        Benchmark(Benchmark&&) noexcept            = delete;
        Benchmark& operator=(const Benchmark&)     = delete;
        Benchmark& operator=(Benchmark&&) noexcept = delete;

        ~Benchmark()
        {
            // Automatic deregistration from the global registry
            std::lock_guard<std::mutex> lock(GetRegistryMutex());
            auto& reg = GetRegistry();
            reg.erase(
                    std::remove_if(
                            reg.begin(),
                            reg.end(),
                            [this](auto const& uptr) { return uptr.get() == this; }),
                    reg.end());
        }

        /// \brief  Estimate per‐Start/Stop overhead (nanoseconds).
        static F64 EstimateTimerOverhead(Int32 iterations = 1'000'000)
        {
            Timer overheadTimer;
            F64 sum = 0.0;
            for (Int32 i = 0; i < iterations; ++i)
            {
                overheadTimer.Reset();
                overheadTimer.Start();
                overheadTimer.Stop();
                sum += overheadTimer.GetElapsed<Nanoseconds>().GetValue();
            }
            return sum / static_cast<F64>(iterations);
        }

        /// \brief  Run this benchmark’s callable config.iterations times,
        ///         collecting min, max, avg, stddev, and percentiles.
        /// \throws std::runtime_error if the user’s callable throws.
        template<typename DesiredUnit>
            requires IsUnitOf<DesiredUnit, Time>
        [[nodiscard]] BenchmarkResult<DesiredUnit> Run()
        {
            assert(("Run() called but m_callable is empty", m_callable));

            BenchmarkResult<DesiredUnit> result;
            result.name          = name;
            result.numIterations = config.iterations;

            // Warmup loop (no timing recorded)
            for (Int32 i = 0; i < config.warmupIterations; ++i)
            {
                invokeOnce();
            }

            // Precompute overhead if requested
            F64 overheadMean = 0.0;
            if (config.accountForOverhead)
            {
                overheadMean = EstimateTimerOverhead(config.iterations);
            }

            std::vector<F64> rawTimings;
            if (config.keepRawTimings)
            {
                rawTimings.reserve(static_cast<std::size_t>(config.iterations));
            }

            // Welford’s one‐pass for mean & variance
            F64 mean    = 0.0;
            F64 M2      = 0.0;
            Int32 count = 0;
            F64 minT    = std::numeric_limits<F64>::infinity();
            F64 maxT    = -std::numeric_limits<F64>::infinity();

            for (Int32 i = 0; i < config.iterations; ++i)
            {
                F64 elapsed;
                try
                {
                    elapsed = invokeOnceTimed(overheadMean);
                } catch (const std::exception& e)
                {
                    throw std::runtime_error(
                            std::string("Benchmark '") + name +
                            "' threw exception on iteration " +
                            std::to_string(i) + ": " + e.what());
                } catch (...)
                {
                    throw std::runtime_error(
                            std::string("Benchmark '") + name +
                            "' threw unknown exception on iteration " +
                            std::to_string(i));
                }

                if (config.keepRawTimings)
                {
                    rawTimings.push_back(elapsed);
                }

                // Welford update
                ++count;
                F64 delta = elapsed - mean;
                mean += delta / static_cast<F64>(count);
                F64 delta2 = elapsed - mean;
                M2 += delta * delta2;
                if (elapsed < minT)
                    minT = elapsed;
                if (elapsed > maxT)
                    maxT = elapsed;
            }

            F64 variance = (count > 1 ? (M2 / static_cast<F64>(count)) : 0.0);
            F64 stddev   = std::sqrt(variance);

            result.averageTime       = UnitCast<DesiredUnit>(Nanoseconds(mean));
            result.minTime           = UnitCast<DesiredUnit>(Nanoseconds(minT));
            result.maxTime           = UnitCast<DesiredUnit>(Nanoseconds(maxT));
            result.standardDeviation = UnitCast<DesiredUnit>(Nanoseconds(stddev));

            if (config.keepRawTimings && !rawTimings.empty())
            {
                std::sort(rawTimings.begin(), rawTimings.end());
                auto at = [&](std::size_t idx) -> F64 {
                    return rawTimings[std::min(idx, rawTimings.size() - 1)];
                };
                F64 med = at(rawTimings.size() / 2);
                F64 p25 = at(rawTimings.size() / 4);
                F64 p75 = at((3 * rawTimings.size()) / 4);

                result.medianTime   = UnitCast<DesiredUnit>(Nanoseconds(med));
                result.percentile25 = UnitCast<DesiredUnit>(Nanoseconds(p25));
                result.percentile75 = UnitCast<DesiredUnit>(Nanoseconds(p75));
            }

            return result;
        }

        //------------------------------------------------------------------------
        // Static Register methods
        //------------------------------------------------------------------------

        /// \brief  Register a void(BenchmarkContext&) callable under defaultConfig.
        template<typename F>
            requires std::is_invocable_r_v<void, F, BenchmarkContext&>
        static void Register(F func, std::string_view benchmarkName)
        {
            auto ptr = std::make_unique<Benchmark>(std::move(func), benchmarkName);
            std::lock_guard<std::mutex> lock(GetRegistryMutex());
            GetRegistry().push_back(std::move(ptr));
        }

        /// \brief  Register a callable with custom config.
        template<typename F>
            requires std::is_invocable_r_v<void, F, BenchmarkContext&>
        static void Register(const BenchmarkConfig& cfg, F func, std::string_view benchmarkName)
        {
            auto ptr = std::make_unique<Benchmark>(cfg, std::move(func), benchmarkName);
            std::lock_guard<std::mutex> lock(GetRegistryMutex());
            GetRegistry().push_back(std::move(ptr));
        }

    private:
        BenchmarkConfig config;
        std::string name;
        Utilities::Callable<void(BenchmarkContext&)> m_callable;

        /// \brief  Invoke m_callable once without timing.
        void invokeOnce()
        {
            BenchmarkContext ctx;
            m_callable(ctx);
        }

        /// \brief  Invoke m_callable once with timing; return elapsed ns minus overhead.
        F64 invokeOnceTimed(F64 overheadMean)
        {
            BenchmarkContext ctx;
            ctx.start();
            m_callable(ctx);
            F64 elapsed = ctx.stop();
            elapsed -= overheadMean;
            if (elapsed < 0.0)
                elapsed = 0.0;
            return elapsed;
        }

        static std::vector<std::unique_ptr<Benchmark>>& GetRegistry()
        {
            static std::vector<std::unique_ptr<Benchmark>> registry;
            return registry;
        }

        static std::mutex& GetRegistryMutex()
        {
            static std::mutex registryMutex;
            return registryMutex;
        }

    public:
        inline static BenchmarkConfig defaultConfig = BenchmarkConfig();

        /// \brief  Runs all registered benchmarks and returns their results.
        template<typename DesiredUnit>
            requires IsUnitOf<DesiredUnit, Time>
        static std::vector<BenchmarkResult<DesiredUnit>> RunAll()
        {
            std::vector<BenchmarkResult<DesiredUnit>> results;
            std::lock_guard<std::mutex> lock(GetRegistryMutex());

            for (auto const& uptr: GetRegistry())
            {
                // Temporarily override with defaultConfig
                BenchmarkConfig orig = uptr->config;
                uptr->config         = defaultConfig;
                results.push_back(uptr->Run<DesiredUnit>());
                uptr->config = orig;
            }
            return results;
        }
        template<typename DesiredUnit>
            requires IsUnitOf<DesiredUnit, Time>
        static void PrintSummaryTable(std::ostream& os,
                                      const std::vector<BenchmarkResult<DesiredUnit>>& results)
        {
            if (results.empty())
            {
                os << "(no benchmarks to display)\n";
                return;
            }

            // 1) Convert each numeric field to a string so we can compute column widths.
            struct RowStrings
            {
                std::string name;
                std::string avg;
                std::string min;
                std::string max;
                std::string stddev;
            };

            std::vector<RowStrings> table;
            table.reserve(results.size());

            // Header labels
            const std::string hdrName   = "Benchmark Name";
            const std::string hdrAvg    = "Avg";
            const std::string hdrMin    = "Min";
            const std::string hdrMax    = "Max";
            const std::string hdrStddev = "StdDev";

            // Convert each field to a string via a temporary ostringstream
            for (auto const& r: results)
            {
                RowStrings row;
                row.name = r.name;

                {
                    std::ostringstream tmp;
                    tmp << r.averageTime;
                    row.avg = tmp.str();
                }
                {
                    std::ostringstream tmp;
                    tmp << r.minTime;
                    row.min = tmp.str();
                }
                {
                    std::ostringstream tmp;
                    tmp << r.maxTime;
                    row.max = tmp.str();
                }
                {
                    std::ostringstream tmp;
                    tmp << r.standardDeviation;
                    row.stddev = tmp.str();
                }

                table.push_back(std::move(row));
            }

            // 2) Determine the maximum content width for each column (header vs. cells).
            size_t wName   = hdrName.size();
            size_t wAvg    = hdrAvg.size();
            size_t wMin    = hdrMin.size();
            size_t wMax    = hdrMax.size();
            size_t wStddev = hdrStddev.size();

            for (auto const& row: table)
            {
                wName   = std::max(wName, row.name.size());
                wAvg    = std::max(wAvg, row.avg.size());
                wMin    = std::max(wMin, row.min.size());
                wMax    = std::max(wMax, row.max.size());
                wStddev = std::max(wStddev, row.stddev.size());
            }

            // 3) Helper lambdas for drawing horizontal borders and printing cells.
            auto drawBorder = [&](void) {
                os << '+'
                   << std::string(wName + 2, '-') << '+'
                   << std::string(wAvg + 2, '-') << '+'
                   << std::string(wMin + 2, '-') << '+'
                   << std::string(wMax + 2, '-') << '+'
                   << std::string(wStddev + 2, '-') << '+'
                   << '\n';
            };

            auto printHeader = [&](void) {
                os << "| " << std::left << std::setw(int(wName)) << hdrName << " "
                   << "| " << std::right << std::setw(int(wAvg)) << hdrAvg << " "
                   << "| " << std::right << std::setw(int(wMin)) << hdrMin << " "
                   << "| " << std::right << std::setw(int(wMax)) << hdrMax << " "
                   << "| " << std::right << std::setw(int(wStddev)) << hdrStddev << " "
                   << "|\n";
            };

            auto printRow = [&](const RowStrings& row) {
                os << "| " << std::left << std::setw(int(wName)) << row.name << " "
                   << "| " << std::right << std::setw(int(wAvg)) << row.avg << " "
                   << "| " << std::right << std::setw(int(wMin)) << row.min << " "
                   << "| " << std::right << std::setw(int(wMax)) << row.max << " "
                   << "| " << std::right << std::setw(int(wStddev)) << row.stddev << " "
                   << "|\n";
            };

            // 4) Actually print the table:
            drawBorder();
            printHeader();
            drawBorder();
            for (auto const& row: table)
            {
                printRow(row);
            }
            drawBorder();
        }
    };

    /// \brief  Prints key statistics of a BenchmarkResult.
    template<typename DesiredUnit>
        requires IsUnitOf<DesiredUnit, Time>
    std::ostream& operator<<(std::ostream& os, BenchmarkResult<DesiredUnit> const& r)
    {
        os << "[" << r.name << "]  "
           << r.numIterations << " iterations\n"
           << "  avg = " << r.averageTime
           << ",  min = " << r.minTime
           << ",  max = " << r.maxTime
           << ",  stddev = " << r.standardDeviation;
        if (r.percentile25.GetValue() > 0.0 || r.medianTime.GetValue() > 0.0)
        {
            os << "\n  p25 = " << r.percentile25
               << ",  median = " << r.medianTime
               << ",  p75 = " << r.percentile75;
        }
        return os;
    }

}// namespace NGIN
