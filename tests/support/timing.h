#pragma once

#include <QTest>

// Marks a test function that checks speed (frame rate, start time, timing
// accuracy) against the reference hardware's requirements. Such checks fail
// on machines far slower than any real operator PC, such as CI's macOS
// virtual machines (no GPU, software video conversion). With
// LUMEN_SKIP_TIMING=1 they are skipped; CI then runs them in a separate,
// advisory step (`ctest -L timing`) whose results are reported but do not fail
// the build. Everywhere else, including Linux and Windows CI, they are strict.
#define LUMEN_TIMING_CHECK()                                                                          \
    do {                                                                                              \
        if (qEnvironmentVariableIntValue("LUMEN_SKIP_TIMING") == 1)                                   \
            QSKIP("speed check: run separately on this machine (LUMEN_SKIP_TIMING=1)");               \
    } while (false)
