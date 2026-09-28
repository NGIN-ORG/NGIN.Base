# Dispatch audit

T08 implementation audit, 2026-09-13. This classifies dispatch sites against R03/R07 and the
reserved-delivery portion of R17. It does not close later admission-capacity, performance, or native-platform
gates. Verification is recorded in Stage1; tuning remains T09.

| Path | Dispatch and ownership decision |
| --- | --- |
| Sequential task entry and successful return | `CanTransfer` checks current executor and charges chain/enclosing work budgets. Refusal queues the root's existing continuation. Return publishes placement and releases execution/parent retention before transfer. |
| Task terminal delivery and independent Operation await | CompleteContinuation publishes handler state before delivery. DeliverChild drops its delivery lease before the handler retires required child ownership. TryDispatchInline checks the target chain executor; refusal queues its reserved slot. Independent observers retain their own placement. |
| Extra child-frame leases and failure unwind | ReleaseFrameReference destroys the final frame before scheduling its retirement observer. Queued bottom-up retirement prevents recursive destruction of an unbounded failed chain and permits release from arbitrary threads. |
| Initial submission, early notification, rejection, discard | SubmitTracked/SignalSubmission publish armed/ready/discard state. A normal accepted queue callback resumes in the executor dispatch scope. A notification preceding setup commit or a discarded item uses the reserved slot, so the submitting or discarding stack cannot become an unauthorized user execution context. |
| TaskContext waits | PendingResume requires setup and outcome publication before delivery. Registration reset and frame-lease release precede user resume. Timer delivery may inline; explicit Yield remains queued even when current and within budget. |
| Executor transitions | SwitchTo uses budgeted transfer or the replacement reserved slot. Target capacity is acquired before the previous guarantee is released. Explicit YieldNow always queues. |
| File and socket completion | The runtime/backend retirement boundary precedes eligible selected-executor delivery. Submission locks are released before user continuation; the pre-admitted ticket handles refusal. Common backend notifications remain queued at the runtime boundary. |
| Resolver | Setup commit and result publication gate delivery; cancellation registrations and removable timer retire first. Discard participates in the same terminal arbitration. Selected-executor inline dispatch has reserved fallback. |
| AsyncGenerator | EndAdvance publishes the boundary, releases its lock and producer execution ownership, then tries the consumer executor. No producer-frame access follows delivery. A retained consumer and its chain slot cover refusal. |
| TaskScope and WhenAny final join | The last retirement notification publishes joined readiness and extracts the retained parent under the state mutex. It releases the mutex before trying inline delivery; refusal uses the parent's existing slot. No state/child access follows user resume. |
| OperationAccess terminal/retirement notifications | Intentionally queued, including already-terminal observations. Reusable callbacks serialize terminal consumption and later frame retirement. A reservation is a delivery capability, not permission to invoke its callback under a caller's locks or on a foreign release thread. Final joins perform the eligible user dispatch separately. |
| CancellationNode raw notifications | Intentionally queued while the cancellation callback's invocation/Reset handoff completes. Reset joins concurrent invocation; a raw cancellation notification is not proof that a backend has retired. |
| Coroutine handle and WorkItem primitives | Raw resume/Invoke remain primitives; tracked uses above establish their authorization and publication. Synchronous Generator and explicit InlineScheduler::Invoke are synchronous APIs. FiberScheduler exposes no current-context/reservation capability and cannot admit a tracked suspension. |
| Runtime host entry | Run's entered callback is synchronous host setup before driving, not a task continuation. Runtime timer/event/stop/queue callbacks run within Batch's shared DispatchBudget after source locks are released. |
| CompletionQueue and schedulers | CompletionQueue invokes outside its queue lock. CooperativeScheduler, ThreadPoolScheduler, and RuntimeLoop provide dispatch-depth/work accounting around source selection, refund empty attempts, and leave queued work untouched on refusal. Reusable completion callbacks move pending chain work out before invoking and never read the chain afterward. |

The audit's two final changes are the TaskScope and WhenAny joins. Regression fixtures retain a child frame
past terminal publication, release it outside the executor, then drive exactly one retirement callback.
They verify that the parent cannot finish before retirement or on the foreign release stack, can finish
inline in the authorized callback, and instead remains pending when that callback exhausts the work budget.
Existing concurrent scope/race tests cover capture retirement and callback reentrancy.

Working cadence: implement a coherent task batch, run focused functional and relevant sanitizer checks,
and run broader suites/benchmarks at the stage gate. Keep logs in the existing build evidence directory;
individual dispatch sites do not require new source archives or benchmark checkpoints.
