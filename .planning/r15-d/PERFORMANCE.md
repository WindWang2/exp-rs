# R15-D Performance

No performance claims. No bottleneck was measured that justified a change
in this track; every fix is correctness/fail-closed. Costs added are
O(path depth) canonicalization per containment check (Windows only, error
path identical) and a few type checks per JSON leaf — both bounded and on
validation paths, not hot loops. Any future perf claim must come with
input data, hardware, warmup, baseline and after measurements per the
track rules.
