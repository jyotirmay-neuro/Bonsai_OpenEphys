/* libshared/oeconnect/include/oeconnect/drift.h */
#ifndef OEC_DRIFT_H
#define OEC_DRIFT_H

#include "oeconnect/types.h"

#ifdef __cplusplus
extern "C" {
#endif

#define OEC_DRIFT_WINDOW 60   /* spec §4.6 — 60-point sliding window */

typedef struct oec_drift_fit oec_drift_fit_t;

OEC_API oec_drift_fit_t *oec_drift_create(void);
OEC_API void             oec_drift_destroy(oec_drift_fit_t *f);

/* Add a (sample_index, host_qpc_ticks) pair. Old points evict FIFO. */
OEC_API void oec_drift_add(oec_drift_fit_t *f, uint64_t sample_index, uint64_t qpc);

/* Number of points currently in the window (0..OEC_DRIFT_WINDOW). */
OEC_API int oec_drift_count(const oec_drift_fit_t *f);

/* Run weighted least-squares on the window. `a` = slope (qpc per sample),
 * `b` = intercept (qpc when sample == 0). Returns OEC_OK or OEC_E_PARSE if
 * fewer than 2 points are available. */
OEC_API oec_status_t oec_drift_fit(const oec_drift_fit_t *f, double *out_a, double *out_b);

/* Reset window. Used by callers that detect divergence. */
OEC_API void oec_drift_reset(oec_drift_fit_t *f);

/* Convenience: predict qpc from sample using the last successful fit. */
OEC_API uint64_t oec_drift_predict_qpc(const oec_drift_fit_t *f, uint64_t sample_index);

/* Residual RMS (in qpc ticks) of the current fit; -1 if no fit. */
OEC_API double oec_drift_residual_rms(const oec_drift_fit_t *f);

#ifdef __cplusplus
}
#endif

#endif /* OEC_DRIFT_H */
