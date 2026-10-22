# R20-05 standalone RED/GREEN runs (2026-10-23, host gcc, -ljsoncpp/-lQt6Core/-lgdal; base=master 28e1891390, head=this branch @79adab3a33)

## WP-ML restore semantics (red_driver: course_home percent + feedback indeterminate)
```
$ ./red_base   # all restored assertions FAIL on base
ASSERTION FAILS on base (=RED evidence): vm.overallPercent == 100
ASSERTION FAILS on base (=RED evidence): found && rowStatus == "indeterminate"
ASSERTION FAILS on base (=RED evidence): p.overallStatus == "indeterminate"
NO-RED: base already behaves as restored
BASE EXIT=0 (0 == RED confirmed: every restored assertion fails on base)
$ ./red_head   # restored assertions HOLD on head
ASSERTION HOLDS on base (no red): vm.overallPercent == 100
ASSERTION HOLDS on base (no red): found && rowStatus == "indeterminate"
ASSERTION HOLDS on base (no red): p.overallStatus == "indeterminate"
RED-CONFIRMED: all restored assertions fail on base (the lost fixes are really missing)
HEAD EXIT=1 (1 == inverted-exit driver: all assertions hold on head = GREEN)
```

## #1652-1 rubric delegation (rb_driver)
```
engine-legal rubric -> ok=0
flat-dialect rubric -> ok=1
OLD-BEHAVIOR (flat dialect still the validator's truth)
BASE EXIT=1
engine-legal rubric -> ok=1
flat-dialect rubric -> ok=0
DELEGATION-OK (engine truth)
HEAD EXIT=0
```

## #1600 sampling stamp (sv_driver, 100x100 GTiff, 9x32x32=9216 of 10000 px)
```
NOT-STAMPED (RED on this tree)
BASE EXIT=1
SAMPLING-STAMPED
HEAD EXIT=0
```

## #1607 residual nesting bomb (jp_driver, 300-deep)
```
$ ./jp_base 2>&1 | tail -2
terminate called after throwing an instance of 'Json::RuntimeError'
  what():  Exceeded stackLimit in readValue().
BASE REAL EXIT=134 (134 = SIGABRT, uncaught Json::RuntimeError)
ledger typed refusal: yes | digest empty refusal: yes
HEAD EXIT=0
```

## R1-review remediation evidence (appended after the P0/P2 fixes)

### P0 — discovery accounting (disc_driver; hidden dir + hidden root file + hidden in-dir file + nested lesson dir)
```
$ ./disc_base   # master 28e1891390
items=2 skips=0 hiddenDirAccounted=0 nestedAccounted=0
SILENT-VANISH (RED on this tree)
BASE EXIT=1
$ ./disc_head   # this branch
items=2 skips=4 hiddenDirAccounted=1 nestedAccounted=1
ACCOUNTING-OK
HEAD EXIT=0
```

### P2 — elongated-raster census mislabel (sv2_driver; 200x40 @ axis 32: 9216 samples >= 8000 px, long-axis columns 32..84 / 116..168 unread)
```
$ ./sv2_head   # per-axis contiguity census condition
ELONGATED-STAMPED
SV2 HEAD EXIT=0
$ ./sv2_base   # master: no stamping at all
ELONGATED-UNMARKED (bad)
SV2 BASE EXIT=1
```
