# FakeDepConfig.cmake — test double for the dependency doctor fixture
# (tests/fixtures/dep_doctor_fixture). Reports version 1.0; the fixture asks
# for 2.0 (stale case) or 1.0 (ok case) and asserts the doctor's behaviour.
set(FakeDep_VERSION 1.0)
set(FakeDep_FOUND TRUE)
