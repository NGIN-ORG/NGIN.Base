/// @file Process.cpp
/// @brief Behavioral tests for cross-platform child-process execution.

#include <NGIN/Async/Cancellation.hpp>
#include <NGIN/Async/Task.hpp>
#include <NGIN/Execution/ThreadPoolScheduler.hpp>
#include <NGIN/IO/Process.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>
#include <thread>

#include <catch2/catch_test_macros.hpp>

#ifndef NGIN_BASE_TEST_PROCESS_FIXTURE_FILENAME
#error "NGIN_BASE_TEST_PROCESS_FIXTURE_FILENAME must be defined for Process tests."
#endif

namespace
{
    using namespace std::chrono_literals;

    [[nodiscard]] NGIN::IO::ProcessOptions FixtureOptions(std::vector<std::string> arguments)
    {
        NGIN::IO::ProcessOptions options;
        options.executable          = NGIN::IO::Path {std::string {"./"} + NGIN_BASE_TEST_PROCESS_FIXTURE_FILENAME};
        options.arguments           = std::move(arguments);
        options.standardOutput.mode = NGIN::IO::ProcessStreamMode::Capture;
        options.standardError.mode  = NGIN::IO::ProcessStreamMode::Capture;
        return options;
    }
}// namespace

TEST_CASE("IO.Process captures output, errors, arguments, and exit status", "[IO][Process]")
{
    auto streams = NGIN::IO::RunProcess(FixtureOptions({"streams"}));
    REQUIRE(streams.HasValue());
    CHECK(streams.Value().exitCode == 0);
    CHECK(streams.Value().standardOutput == "standard-output");
    CHECK(streams.Value().standardError == "standard-error");

    auto arguments = NGIN::IO::RunProcess(FixtureOptions({"arguments", "plain", "with spaces", R"(quote\"slash\\)"}));
    REQUIRE(arguments.HasValue());
    CHECK(arguments.Value().standardOutput == R"(plain|with spaces|quote\"slash\\)");

    auto exitResult = NGIN::IO::RunProcess(FixtureOptions({"exit", "23"}));
    REQUIRE(exitResult.HasValue());
    CHECK(exitResult.Value().exitCode == 23);
}

TEST_CASE("IO.Process applies environment and working-directory options", "[IO][Process]")
{
    auto environment = FixtureOptions({"environment", "NGIN_BASE_PROCESS_VALUE"});
    environment.environment.push_back({"NGIN_BASE_PROCESS_VALUE", "value with spaces"});
    const auto environmentResult = NGIN::IO::RunProcess(std::move(environment));
    REQUIRE(environmentResult.HasValue());
    CHECK(environmentResult.Value().standardOutput == "value with spaces");

    auto workingDirectory             = FixtureOptions({"working-directory"});
    workingDirectory.workingDirectory = NGIN::IO::Path {std::filesystem::temp_directory_path().generic_string()};
    workingDirectory.executable       = NGIN::IO::Path {std::filesystem::absolute(
                                                                std::filesystem::path {NGIN_BASE_TEST_PROCESS_FIXTURE_FILENAME})
                                                                .generic_string()};
    const auto directoryResult        = NGIN::IO::RunProcess(std::move(workingDirectory));
    REQUIRE(directoryResult.HasValue());
    CHECK(std::filesystem::equivalent(
            std::filesystem::path {directoryResult.Value().standardOutput}, std::filesystem::temp_directory_path()));
}

TEST_CASE("IO.Process supports observers, discarded streams, and file output", "[IO][Process]")
{
    std::string observedOutput;
    std::string observedError;
    auto        observed            = FixtureOptions({"streams"});
    observed.standardOutputObserver = [&](std::string_view chunk) { observedOutput.append(chunk); };
    observed.standardErrorObserver  = [&](std::string_view chunk) { observedError.append(chunk); };
    const auto observedResult       = NGIN::IO::RunProcess(std::move(observed));
    REQUIRE(observedResult.HasValue());
    CHECK(observedOutput == "standard-output");
    CHECK(observedError == "standard-error");

    auto discarded                = FixtureOptions({"streams"});
    discarded.standardOutput.mode = NGIN::IO::ProcessStreamMode::Discard;
    discarded.standardError.mode  = NGIN::IO::ProcessStreamMode::Discard;
    const auto discardedResult    = NGIN::IO::RunProcess(std::move(discarded));
    REQUIRE(discardedResult.HasValue());
    CHECK(discardedResult.Value().standardOutput.empty());
    CHECK(discardedResult.Value().standardError.empty());

    const auto outputPath   = std::filesystem::current_path() / "ngin-base-process-output.txt";
    auto       file         = FixtureOptions({"streams"});
    file.standardOutput     = {NGIN::IO::ProcessStreamMode::File, NGIN::IO::Path {outputPath.generic_string()}, false};
    file.standardError.mode = NGIN::IO::ProcessStreamMode::Discard;
    const auto fileResult   = NGIN::IO::RunProcess(std::move(file));
    REQUIRE(fileResult.HasValue());
    std::ifstream output {outputPath};
    REQUIRE(output.good());
    const std::string contents {std::istreambuf_iterator<char> {output}, std::istreambuf_iterator<char> {}};
    CHECK(contents == "standard-output");
    output.close();
    std::filesystem::remove(outputPath);
}

TEST_CASE("IO.Process enforces timeout, cancellation, and output limits", "[IO][Process]")
{
    auto timeout           = FixtureOptions({"sleep", "5000"});
    timeout.timeout        = 25ms;
    const auto timedResult = NGIN::IO::RunProcess(std::move(timeout));
    REQUIRE(timedResult.HasValue());
    CHECK(timedResult.Value().timedOut);

    NGIN::Async::CancellationSource source;
    auto                            canceled = FixtureOptions({"sleep", "5000"});
    canceled.cancellation                    = source.GetToken();
    std::jthread canceler {[&source] {
        std::this_thread::sleep_for(25ms);
        source.Cancel();
    }};
    const auto   canceledResult = NGIN::IO::RunProcess(std::move(canceled));
    REQUIRE(canceledResult.HasValue());
    CHECK(canceledResult.Value().canceled);

    const auto probeStarted  = std::chrono::steady_clock::now();
    auto       probed        = FixtureOptions({"sleep", "5000"});
    probed.cancellationProbe = [probeStarted] {
        return std::chrono::steady_clock::now() - probeStarted >= 25ms;
    };
    const auto probedResult = NGIN::IO::RunProcess(std::move(probed));
    REQUIRE(probedResult.HasValue());
    CHECK(probedResult.Value().canceled);

    auto limited               = FixtureOptions({"spam", "10000"});
    limited.maximumOutputBytes = 128;
    const auto limitedResult   = NGIN::IO::RunProcess(std::move(limited));
    REQUIRE(limitedResult.HasValue());
    CHECK(limitedResult.Value().outputLimitExceeded);
    CHECK(limitedResult.Value().standardOutput.size() == 128);
}

TEST_CASE("IO.Process reports invalid starts and enforces single wait", "[IO][Process]")
{
    NGIN::IO::ProcessOptions missing;
    missing.executable       = NGIN::IO::Path {"__ngin_base_missing_process__"};
    const auto missingResult = NGIN::IO::Process::Start(std::move(missing));
    REQUIRE_FALSE(missingResult.HasValue());
    CHECK(missingResult.Error().code == NGIN::IO::ProcessErrorCode::StartFailed);

    auto started = NGIN::IO::Process::Start(FixtureOptions({"exit", "0"}));
    REQUIRE(started.HasValue());
    auto process = std::move(started).TakeValue();
    REQUIRE(process.IsValid());
    const auto firstWait = process.Wait();
    REQUIRE(firstWait.HasValue());
    const auto secondWait = process.Wait();
    REQUIRE_FALSE(secondWait.HasValue());
    CHECK(secondWait.Error().code == NGIN::IO::ProcessErrorCode::AlreadyWaited);
}

TEST_CASE("IO.Process async execution uses the caller-owned executor and context cancellation", "[IO][Process]")
{
    NGIN::Execution::ThreadPoolScheduler scheduler {1};
    NGIN::Async::CancellationSource      source;
    NGIN::Async::TaskContext             context {scheduler, source.GetToken()};

    auto success = NGIN::Async::SyncWait(context, NGIN::IO::RunProcessAsync(context, FixtureOptions({"exit", "17"})));
    REQUIRE(success.HasValue());
    CHECK(success.Value().exitCode == 17);

    std::jthread canceler {[&source] {
        std::this_thread::sleep_for(25ms);
        source.Cancel();
    }};
    auto         canceled = NGIN::Async::SyncWait(
            context,
            NGIN::IO::RunProcessAsync(context, FixtureOptions({"sleep", "5000"})));
    REQUIRE(canceled.HasValue());
    CHECK(canceled.Value().canceled);
}

TEST_CASE("IO.Process timeout terminates the isolated descendant tree", "[IO][Process]")
{
    using namespace std::chrono_literals;

    const auto marker = std::filesystem::current_path() / "ngin-base-process-descendant-marker.txt";
    std::filesystem::remove(marker);

    auto tree         = FixtureOptions({"process-tree", marker.string()});
    tree.timeout      = 150ms;
    const auto result = NGIN::IO::RunProcess(std::move(tree));
    REQUIRE(result.HasValue());
    CHECK(result.Value().timedOut);

    std::this_thread::sleep_for(900ms);
    CHECK_FALSE(std::filesystem::exists(marker));
    std::filesystem::remove(marker);
}
