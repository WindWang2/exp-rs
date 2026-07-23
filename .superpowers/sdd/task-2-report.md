# Task 2 report: cross-validation Task Center slices

## Scope

Changed only `tests/test_classification_task_center.cpp` plus this report.
Added two `[classify][cv]` Task Center seams:

- a known-good three-Gaussian `RsCvTask` completing with `meanAccuracy > 0.85`
  and a `stdAccuracy` field;
- an empty-input `RsCvTask` surfacing as a non-empty Task Center failure.

The Task 1 commit (`709153c0cc`) already provided `submitCrossValidation`, including
Task Center error/cancellation conversion and `worker->cancel()` forwarding. No
production-window migration or production-code change was required.

## RED evidence

After adding the successful CV test before its Gaussian fixture and classifier
include, the requested stale executable invocation did not rebuild and reported:

```text
$ build-task-center-26/tests/test_classification_task_center "[cv]"
Filters: [cv]
No test cases matched '[cv]'
No tests ran
```

Building the target supplied the actual RED evidence:

```text
$ cmake --build build-task-center-26 --target test_classification_task_center -j2
.../test_classification_task_center.cpp:238:3: error: ‘makeGaussianData’ was not declared in this scope
.../test_classification_task_center.cpp:242:31: error: ‘RsClassifierNormalBayes’ was not declared in this scope
make: *** [Makefile:2090: test_classification_task_center] Error 2
```

## GREEN evidence

Added the local known-good Gaussian fixture (copied in behavior from
`tests/test_cross_validation.cpp`) and the `RsClassifierNormalBayes` include.

```text
$ cmake --build build-task-center-26 --target test_classification_task_center -j2 \
  && build-task-center-26/tests/test_classification_task_center "[cv]"
[100%] Built target test_classification_task_center
All tests passed (3 assertions in 1 test case)
```

Then added the empty-data failure slice. It was green on first execution because
the existing Task 1 `submitCrossValidation` bridge already maps its worker error
to a failed Task Center record:

```text
$ cmake --build build-task-center-26 --target test_classification_task_center -j2 \
  && build-task-center-26/tests/test_classification_task_center "[cv]"
[100%] Built target test_classification_task_center
All tests passed (5 assertions in 2 test cases)
```

## Regression and self-review

```text
$ build-task-center-26/tests/test_classification_task_center "[classify]"
All tests passed (18 assertions in 5 test cases)

$ git diff --check
(no output; exit 0)
```

Manual scoped-diff review found no production/UI migration, no unrelated source
changes, and no whitespace errors. Existing build output contains pre-existing
Qt deprecation and QCA include-path warnings; the target still completes
successfully.

## Review follow-up

Added a CV-specific cancellation seam through `submitCrossValidation`. A
blocking `RsCvTask` records its `cancel()` invocation; after `cancelTask()` the
test proves that hook has run while the Task Center record remains `Running`,
then releases the worker and requires the terminal `Canceled` state. The
successful CV slice now explicitly requires both `meanAccuracy` and
`stdAccuracy` payload members.

This follow-up test is necessarily test-after for the existing Task 1 helper:
`submitCrossValidation` and its cancellation forwarding were already committed
before Task 2 began. The prior TDD ordering deviation cannot be recreated
honestly without rewriting history or temporarily removing the behavior, so it
is documented rather than represented as fresh RED evidence.

Verification after this follow-up:

```text
$ cmake --build build-task-center-26 --target test_classification_task_center -j2
[100%] Built target test_classification_task_center

$ build-task-center-26/tests/test_classification_task_center "[cv]"
All tests passed (11 assertions in 3 test cases)

$ build-task-center-26/tests/test_classification_task_center "[classify]"
All tests passed (24 assertions in 6 test cases)

$ git diff --check
(no output; exit 0)
```
