# NetworkPlan

Purpose: address documentation gaps and design risks called out in the Network.md review, and allow breaking changes to
make the NGIN::Net API and runtime more explicit, safer, and more portable in behavior.

Constraints and priorities:
- Must build and run with exceptions disabled (engine requirement).
- Prioritize performance and ease of use for consumers.

## 1) Clarify Scope and Audience

- Declare whether NGIN::Net is "expert-only" low-level API or a safe base layer for broader use.
- If expert-only, state required prior knowledge (non-blocking IO, readiness vs completion, cancellation pitfalls).
- If reusable base layer, prioritize explicit invariants and uniform behavior guarantees.

## 2) Error Model: Single, Explained Story

Target: a coherent and documented error contract.

- Add a dedicated "Error Model and Rationale" section.
- Define exactly how failures surface for each class of API:
  - Try* methods
  - Async methods
  - Close/Shutdown
  - Cancellation
- Explain why WouldBlock is not exceptional, and how cancellation is surfaced without exceptions.

Breaking-change options (exceptions disabled means only Option A is viable):
- Option A: unify on NetExpected for both Try* and async (async returns Task<NetExpected<T>>).

Decision criteria:
- Hot-path performance vs consistency.
- Ease of interop with engine-style error handling.
- Clear guidance for user code and tests.

## 3) NetworkDriver Invariants (Hard Rules)

Add a section with explicit invariants and failure modes:

- Driver ownership and lifetime rules:
  - A socket may be used with exactly one NetworkDriver for async operations.
  - NetworkDriver must outlive all outstanding async operations created with it.
- Threading rules:
  - Whether Run/PollOnce can be called concurrently.
  - Whether driver methods are thread-safe.
- Socket migration:
  - Whether a socket may move between drivers after an operation completes.
- Multi-driver usage:
  - Whether two drivers may wait on the same socket (likely "no").

Breaking-change options:
- Add a "DriverHandle" or "DriverRef" stored in sockets/adapters to enforce binding.
- Require explicit Bind/Unbind methods on sockets/adapters that state the driver ownership.
- Encode driver association into the transport adapters to make misuse impossible.

## 4) Platform Behavioral Differences

Add a section that clearly states what is guaranteed across platforms and what is not:

- Ordering guarantees (per operation type) and whether they are strict or best-effort.
- Cancellation timing differences (IOCP vs readiness loop) and what callers should expect.
- Progress guarantees under load (readiness polling vs completion semantics).

Documentation deliverable:
- "Behavioral Differences" section with a guarantee table.
- Explicit note that async methods are completion-based on Windows and readiness-based elsewhere.

## 5) Lifetimes and Ownership Rules

Add a dedicated "Lifetimes and Ownership" section with explicit, easy-to-scan rules:

- Buffer ownership and when payload spans are valid.
- Whether async operations hold references to buffers across suspension points.
- BufferPool invariants and non-thread-safety.
- Transport adapter lifetime requirements (driver and socket must outlive adapter).

Breaking-change options:
- Introduce owning message types for IDatagramChannel (e.g., ReceivedDatagram owns Buffer).
- Provide "ReceiveInto" style APIs that return size + endpoint and keep ownership explicit.

## 6) Builder Rationale or Replacement

Document why ByteStreamBuilder / DatagramBuilder exist, or replace them.

Breaking-change options:
- Replace with explicit constructor or free function:
  - e.g., MakeTcpByteStream(TcpSocket&&, NetworkDriver&).
- If builders stay, document enforced invariants and why the type system cannot express them.

## 7) Allocation and Blocking Guarantees

Create a small API contract table for hot-path users:

| API Group | Allocates? | Syscalls? | Can Block? | Notes |
|----------|------------|-----------|------------|-------|
| Try*     | No         | Yes       | No         | Returns WouldBlock |
| Async    | No (except driver internals) | Yes | Suspends | Returns NetExpected |
| Builders/Adapters | Yes (std::unique_ptr) | No | No | Construction only |

Adjust as needed based on real behavior and platform differences.

## 8) Usage Example (End-to-End)

Add a real, minimal example that compiles:

- NetworkDriver creation and lifetime.
- Threading model (Run on a dedicated thread or PollOnce loop).
- TCP connect and framing.
- Proper error handling and cancellation.
- Shutdown and cleanup.

If async usage is core, the example should show TaskContext usage and a working driver loop.

## 9) Implementation Changes (Ordered)

1. Lock the error model: async returns Task<T> and exposes AsyncExpected<T> via co_await/Get; no throws in Net or Async paths.
2. Rename NetErrc -> NetErrorCode for style consistency, then extend it (and mapping) to cover cancellation and input/size errors:
   - Add at least Canceled and InvalidArgument (or BufferTooSmall) to avoid "Unknown" overload.
3. Async core changes (required for no-exceptions builds):
   - Replace TaskCanceled exceptions with explicit error results.
   - Update Task/TaskContext/WhenAll/WhenAny to propagate errors as values.
   - Remove or gate throw sites in async and fiber code paths.
4. NetworkDriver changes:
   - Awaiters return AsyncExpected<void> instead of throwing.
   - Cancellation maps to AsyncErrorCode::Canceled.
5. Socket async changes:
   - TcpSocket/UdpSocket/TcpListener async APIs return Task<T> and propagate AsyncExpected.
   - Replace throw-based error handling with AsyncExpected propagation.
6. Transport adapter changes:
   - TcpByteStream/UdpDatagramChannel return errors instead of throwing for missing driver or invalid buffer.
   - ByteStreamBuilder/DatagramBuilder return NetExpected<unique_ptr<...>> or be replaced with Make* functions.
7. Filter changes:
   - LengthPrefixedMessageStream returns AsyncExpected for size/EOF errors and cancellation.
8. Documentation updates:
   - Network.md + include/NGIN/Net/README.md to reflect no-exception rules and invariants.
9. Test updates:
   - Add no-exception builds (if CI supports), and error-path tests for cancellation and invalid inputs.

## 10) API Changes Checklist (Breaking Changes Allowed)

- Decide and document the final error model.
- Decide how driver ownership is expressed or enforced.
- Decide whether builders remain or are replaced.
- Decide whether datagram receives return owned data or require explicit buffers.
- Update headers + Network.md + include/NGIN/Net/README.md to match.

## 11) Deliverables

- Update `docs/Network.md` with:
  - Error Model and Rationale
  - NetworkDriver Invariants
  - Behavioral Differences
  - Lifetimes and Ownership
  - Allocation/Blocking table
  - Full end-to-end example
- Update `include/NGIN/Net/README.md` to mirror the contract-level details.
- If API changes are accepted, update headers and add migration notes.

## 12) Decisions Required (To Resolve Before Changes)

Resolved decisions:
- Async error type: separate AsyncError (not NetError).
- Driver binding: enforce in types (at least adapters; ideally sockets too).
- Builders: replace with Make* free functions returning NetExpected.
- Naming: use AsyncErrorCode and NetErrorCode.

Remaining decisions:
- Do we add dedicated NetErrorCode values for InvalidArgument and BufferTooSmall, or reuse Unknown?
- Is the target user expert-only, or a broader base-layer consumer?


