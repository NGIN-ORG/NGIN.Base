#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/LocalFileSystem.hpp>
#include <NGIN/IO/RuntimeRunner.hpp>

#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace
{
    using Clock = std::chrono::steady_clock;
    double Microseconds(Clock::duration duration)
    {
        return std::chrono::duration<double, std::micro>(duration).count();
    }
    void Report(const char* backend, const char* operation, std::vector<double> samples)
    {
        std::sort(samples.begin(), samples.end());
        double sum = 0;
        for (double value: samples)
            sum += value;
        std::cout << backend << '_' << operation << " samples=" << samples.size()
                  << " p50_us=" << samples[(samples.size() - 1) * 50 / 100]
                  << " p95_us=" << samples[(samples.size() - 1) * 95 / 100]
                  << " p99_us=" << samples[(samples.size() - 1) * 99 / 100]
                  << " mean_us=" << sum / static_cast<double>(samples.size()) << '\n';
    }
    template<typename Completion>
    void RequireSuccess(const Completion& result)
    {
        if (!result.Succeeded())
            throw std::runtime_error("file benchmark operation failed");
    }
    struct TemporaryFile
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("ngin-runtime-file-benchmark-" + std::to_string(Clock::now().time_since_epoch().count()));
        ~TemporaryFile()
        {
            std::error_code ignored;
            std::filesystem::remove(path, ignored);
        }
    };
}// namespace

int main()
{
    try
    {
        constexpr std::size_t iterations  = 2000;
        constexpr std::size_t payloadSize = 64 * 1024;
        TemporaryFile         temporary;
        {
            std::ofstream     output(temporary.path, std::ios::binary);
            const std::string data(payloadSize, 'x');
            output.write(data.data(), static_cast<std::streamsize>(data.size()));
            if (!output)
                throw std::runtime_error("file benchmark setup failed");
        }
        NGIN::Execution::ThreadPoolScheduler scheduler(1);
        NGIN::Async::TaskContext             context(scheduler);
        std::vector<NGIN::Byte>              buffer(payloadSize);
        std::vector<double>                  samples;
        samples.reserve(iterations);
        using Runtime = NGIN::IO::Runtime;
        for (auto preference: {Runtime::FileBackendPreference::Native, Runtime::FileBackendPreference::Fallback})
        {
            const char*               name = preference == Runtime::FileBackendPreference::Native ? "native" : "fallback";
            const auto                cold = Clock::now();
            Runtime                   runtime({.files = {.backendPreference = preference}});
            NGIN::IO::RuntimeRunner   runner(runtime);
            NGIN::IO::LocalFileSystem files(runtime);
            NGIN::IO::FileOpenOptions options;
            options.access      = NGIN::IO::FileAccess::ReadWrite;
            options.disposition = NGIN::IO::FileCreateDisposition::OpenExisting;
            auto opened         = NGIN::Async::SyncWait(context, files.OpenFileAsync(context, NGIN::IO::Path(temporary.path.string().c_str()), options));
            RequireSuccess(opened);
            auto file = std::move(opened.Value());
            std::cout << name << " cold_open_us=" << Microseconds(Clock::now() - cold)
                      << " backend=" << static_cast<int>(runtime.GetFileBackend()) << " bytes=" << payloadSize << '\n';
            for (int write = 0; write != 2; ++write)
            {
                samples.clear();
                for (std::size_t index = 0; index < iterations; ++index)
                {
                    const auto start  = Clock::now();
                    auto       result = write
                                                ? NGIN::Async::SyncWait(context, file.WriteAtAsync(context, 0, buffer))
                                                : NGIN::Async::SyncWait(context, file.ReadAtAsync(context, 0, buffer));
                    RequireSuccess(result);
                    if (result.Value() != payloadSize)
                        throw std::runtime_error("file benchmark transferred fewer bytes than requested");
                    samples.push_back(Microseconds(Clock::now() - start));
                }
                Report(name, write ? "write_at_64k" : "read_at_64k", samples);
            }
            RequireSuccess(NGIN::Async::SyncWait(context, file.FlushAsync(context)));
            RequireSuccess(NGIN::Async::SyncWait(context, file.CloseAsync(context)));
        }
    } catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
