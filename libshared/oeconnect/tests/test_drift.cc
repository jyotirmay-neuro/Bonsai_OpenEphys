#include <gtest/gtest.h>
#include <cmath>

extern "C" {
#include "oeconnect/drift.h"
}

TEST(Drift, EmptyFitFails) {
    auto *f = oec_drift_create();
    double a = 0, b = 0;
    EXPECT_EQ(oec_drift_fit(f, &a, &b), OEC_E_PARSE);
    oec_drift_destroy(f);
}

TEST(Drift, RecoversLinearRelation) {
    auto *f = oec_drift_create();
    /* qpc = 100 + 33 * sample */
    for (uint64_t s = 0; s < 50; ++s) {
        oec_drift_add(f, s, 100 + 33 * s);
    }
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_NEAR(a, 33.0, 1e-9);
    EXPECT_NEAR(b, 100.0, 1e-6);
}

TEST(Drift, WindowEvictsOldPoints) {
    auto *f = oec_drift_create();
    for (int i = 0; i < OEC_DRIFT_WINDOW + 5; ++i) {
        oec_drift_add(f, i, i * 10);
    }
    EXPECT_EQ(oec_drift_count(f), OEC_DRIFT_WINDOW);
    oec_drift_destroy(f);
}

TEST(Drift, PredictMatchesFit) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 50; ++s) oec_drift_add(f, s, 1000 + 7 * s);
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_EQ(oec_drift_predict_qpc(f, 100), (uint64_t)std::llround(7.0 * 100 + 1000));
    oec_drift_destroy(f);
}

TEST(Drift, ResidualSmallForPerfectLine) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 50; ++s) oec_drift_add(f, s, 5 + 3 * s);
    double a = 0, b = 0;
    ASSERT_EQ(oec_drift_fit(f, &a, &b), OEC_OK);
    EXPECT_LT(oec_drift_residual_rms(f), 1e-6);
    oec_drift_destroy(f);
}

TEST(Drift, ResetClears) {
    auto *f = oec_drift_create();
    for (uint64_t s = 0; s < 10; ++s) oec_drift_add(f, s, s);
    oec_drift_reset(f);
    EXPECT_EQ(oec_drift_count(f), 0);
    oec_drift_destroy(f);
}
