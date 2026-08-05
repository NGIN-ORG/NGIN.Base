# Process execution

`NGIN::IO::Process` starts child processes directly from an executable and an
argument vector. It does not invoke a shell or interpret quoting, pipes,
redirection operators, environment substitutions, or wildcard syntax.

Use `RunProcess()` for a single synchronous invocation. Use `Process::Start()`
when the caller needs to observe liveness, request termination, or wait later.
`RunProcessAsync()` schedules the blocking wait on the executor owned by the
provided `TaskContext`; it creates no hidden process thread or global runtime.
Reserve an executor worker for each concurrently running process.

## Streams and output

Each standard stream can be inherited, captured, discarded, or redirected to
a file. Standard output and error are captured separately. Incremental
observers receive byte chunks as they are accepted into the capture buffer;
they are not line callbacks and chunk boundaries have no textual meaning.

`maximumOutputBytes` is a combined limit across captured standard output and
standard error. Exceeding it records `outputLimitExceeded` and terminates the
isolated child tree.

## Environment

`inheritEnvironment` controls whether the child starts from the parent
environment. Entries in `environment` then add or replace variables. An entry
whose value is absent removes that variable. Setting `inheritEnvironment` to
false provides an explicit replacement environment.

## Cancellation and lifetime

Timeouts, `CancellationToken`, and `cancellationProbe` are checked while
waiting. The probe exists for integration with external cancellation sources,
such as a command-line signal flag, and must remain cheap and thread-safe.

POSIX cancellation first sends `SIGTERM`, waits `terminationGracePeriod`, and
then sends `SIGKILL`. Windows uses job-object termination because Windows has
no portable graceful signal for arbitrary console and GUI children. With
`isolateProcessTree` enabled (the default), descendants are assigned to the
same POSIX process group or Windows job object.

Destroying a valid `Process` before `Wait()` completes terminates and reaps the
owned child. A process may be waited only once.

## Example

```cpp
NGIN::IO::ProcessOptions options;
options.executable = NGIN::IO::Path {"compiler"};
options.arguments = {"--version"};
options.standardOutput.mode = NGIN::IO::ProcessStreamMode::Capture;
options.standardError.mode = NGIN::IO::ProcessStreamMode::Capture;
options.timeout = std::chrono::seconds {5};

auto result = NGIN::IO::RunProcess(std::move(options));
if (!result)
{
    Report(result.Error().message);
    return;
}

Use(result->standardOutput);
```

If shell behavior is intentionally required, invoke the desired shell as the
executable and pass its command-string argument explicitly. That decision then
belongs to the caller and is visible in its policy and tests.
