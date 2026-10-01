# Evidence Dossier: ASan Heap-Use-After-Free in WorkflowRunCoordinator

**Date**: 2026-10-01  
**Milestone**: M0  
**Target Defect**: R2 / WP-B (ASan Heap-Use-After-Free in `std::map<std::string, quint64>::find`)  
**Report Source**: Explorer Survey 2 (`analysis.md` & `handoff.md`)  

---

## 1. Defect Identification & Symbol Isolation

Through full repository codebase analysis, `std::map<std::string, quint64>::find` exists at **exactly one location**:
- **File**: `src/workflow/workflow_run_coordinator.cpp`
- **Line**: 424
- **Context**: Inside `WorkflowRunCoordinator::persistRun(PersistRequest request)`
  ```cpp
  std::lock_guard<std::mutex> io( m_checkpointIoMutex );
  bool superseded = false;
  {
      std::lock_guard<std::mutex> lock( m_mutex );
      const auto it = m_latestPersistSeq.find( request.runId ); // <--- LINE 424
      superseded = it != m_latestPersistSeq.end() && request.seq < it->second;
  }
  ```

---

## 2. Complete AddressSanitizer Call Stacks

### 2.1 The ASan Report Summary
```
=================================================================
==28341==ERROR: AddressSanitizer: heap-use-after-free on address 0x608000041280 at pc 0x7f83a45c8290 bp 0x7f8398df5a10 sp 0x7f8398df5a08
READ of size 8 at 0x608000041280 thread T2 (JobWorker_1)
    #0 0x7f83a45c828f in std::_Rb_tree<std::string, std::pair<std::string const, unsigned long long>, ...>::find(std::string const&) const
    #1 0x7f83a45c7f8a in std::map<std::string, unsigned long long, ...>::find(std::string const&)
    #2 0x7f83a45c479e in sicnu::workflow::WorkflowRunCoordinator::persistRun(sicnu::workflow::WorkflowRunCoordinator::PersistRequest)
       src/workflow/workflow_run_coordinator.cpp:424
    #3 0x7f83a45cb214 in sicnu::workflow::WorkflowRunCoordinator::onTaskUpdated(AlgorithmTaskInfo const&)
       src/workflow/workflow_run_coordinator.cpp:745
    #4 0x7f83a510f88a in TaskCenter::flushPendingSignals()
       src/processing/framework/task_center.cpp:1287
    #5 0x7f83a511452e in TaskCenter::onJobRecord(sicnu::jobs::JobRecord const&)
       src/processing/framework/task_center.cpp:1828
    #6 0x7f83a48e2340 in sicnu::jobs::JobEngine::finishJobLocked(...)
       src/jobs/job_engine.cpp:1006
    #7 0x7f83a48e1a50 in sicnu::jobs::JobEngine::workerLoop(...)
       src/jobs/job_engine.cpp:895
    #8 0x7f83a3ec0608 in execute_native_thread_routine
    #9 0x7f83a37dcac2 in start_thread nptl/pthread_create.c:442
    #10 0x7f83a385ea3f in clone3 ../sysdeps/unix/sysv/linux/x86_64/clone3.S:81

0x608000041280 is located 32 bytes inside of 48-byte region [0x608000041260,0x608000041290)
freed by thread T0 (Main) here:
    #0 0x7f83a54b38d0 in operator delete(void*, unsigned long)
    #1 0x7f83a45c6128 in std::_Rb_tree<std::string, std::pair<std::string const, unsigned long long>, ...>::_M_erase(...)
    #2 0x7f83a45c5890 in std::map<std::string, unsigned long long, ...>::~map()
    #3 0x7f83a45c3b12 in sicnu::workflow::WorkflowRunCoordinator::~WorkflowRunCoordinator()
       src/workflow/workflow_run_coordinator.cpp:258
    #4 0x7f83a3782b24 in __run_exit_handlers stdlib/exit.c:108
    #5 0x7f83a3782ce9 in exit stdlib/exit.c:139
    #6 0x7f83a3767253 in __libc_start_main_impl ../csu/libc-start.c:381
    #7 0x55d2e38910e4 in _start

previously allocated by thread T2 (JobWorker_1) here:
    #0 0x7f83a54b2810 in operator new(unsigned long)
    #1 0x7f83a45c7112 in std::_Rb_tree<std::string, std::pair<std::string const, unsigned long long>, ...>::_M_emplace_hint_unique(...)
    #2 0x7f83a45c68ae in std::map<std::string, unsigned long long, ...>::operator[](std::string const&)
    #3 0x7f83a45c3820 in sicnu::workflow::WorkflowRunCoordinator::capturePersistLocked(...)
       src/workflow/workflow_run_coordinator.cpp:404
    #4 0x7f83a45cae50 in sicnu::workflow::WorkflowRunCoordinator::onTaskUpdated(AlgorithmTaskInfo const&)
       src/workflow/workflow_run_coordinator.cpp:726
```

---

## 3. Step-by-Step Anatomy of the Race

1. **Step 1: Signal Connection**:
   In `WorkflowRunCoordinator::startTrackedPipeline` (`workflow_run_coordinator.cpp:577-578`), the coordinator connects to `TaskCenter::taskUpdated` using `Qt::DirectConnection`. Consequently, `onTaskUpdated` is invoked synchronously on the worker thread completing the task.
2. **Step 2: Terminal State Flipped Synchronously**:
   In `onTaskUpdated` (`workflow_run_coordinator.cpp:726-747`), when all pipeline steps complete, `finalizeRunLocked` marks `run->state()` as `Completed` and releases `m_locksByRunId`.
3. **Step 3: Checkpoint Persistence Runs Outside Mutex**:
   `capturePersistLocked` allocates a node in `m_latestPersistSeq` via `operator[]` (line 404). Then `m_mutex` is unlocked. The worker thread calls `persistRun` outside `m_mutex`.
4. **Step 4: Checkpoint I/O Serialization**:
   Inside `persistRun`, the worker acquires `m_checkpointIoMutex` to serialize disk writing and artifact cleanup.
5. **Step 5: Premature Test Exit**:
   Concurrently, the main test thread in `tests/test_execution_plane_9.cpp:782-803` evaluates:
   ```cpp
   REQUIRE( waitForCondition9( [&] {
       const auto snap = fx.coordinator.runForPipeline( pipelineId );
       return snap && snap->state() == WorkflowRunState::Completed;
   } ) );
   REQUIRE( fx.coordinator.explainDump().contains( QLatin1String( "runLocks held: 0" ) ) );
   ```
   Because `state() == Completed` and `runLocks held: 0` were set inside `finalizeRunLocked` before `persistRun` finished, this assertion passes immediately.
6. **Step 6: Fixture and Process Teardown Inversion**:
   The test function exits. `CoordinatorFixture9 fx` has no destructor and does not join workers or drain coordinator persists. The process exits, invoking `atexit` destructors. `WorkflowRunCoordinator::s_instance` destructor frees the red-black tree nodes of `m_latestPersistSeq`.
7. **Step 7: Crash**:
   The worker thread, still inside `persistRun`, executes `m_latestPersistSeq.find(request.runId)` on line 424, traversing freed tree nodes and triggering the ASan Heap-Use-After-Free.

---

## 4. Remediation Evidence & Proof Plan

- Adding atomic in-flight counter `m_inFlightPersists` and `drainPersists()` in `WorkflowRunCoordinator`.
- Calling `coordinator.shutdownForTests()` and `engine.shutdownForTests()` in fixture destructors.
- Calling `fx.coordinator.drainPersists()` in M7 test case before post-completion assertions.
- Verification: 50 consecutive runs of `test_execution_plane_9` under ASan with zero defects.
