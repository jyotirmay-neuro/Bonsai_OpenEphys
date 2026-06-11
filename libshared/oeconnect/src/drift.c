/* libshared/oeconnect/src/drift.c */
#define OEC_BUILDING_LIB
#include "oeconnect/drift.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

struct oec_drift_fit {
    double sample[OEC_DRIFT_WINDOW];
    double qpc[OEC_DRIFT_WINDOW];
    int    count;
    int    head;        /* next write index (FIFO) */
    int    fitted;
    double a;
    double b;
    double rms;
};

oec_drift_fit_t *oec_drift_create(void) {
    return (oec_drift_fit_t *)calloc(1, sizeof(oec_drift_fit_t));
}

void oec_drift_destroy(oec_drift_fit_t *f) { free(f); }

void oec_drift_add(oec_drift_fit_t *f, uint64_t sample_index, uint64_t qpc) {
    if (!f) return;
    f->sample[f->head] = (double)sample_index;
    f->qpc[f->head]    = (double)qpc;
    f->head = (f->head + 1) % OEC_DRIFT_WINDOW;
    if (f->count < OEC_DRIFT_WINDOW) ++f->count;
}

int oec_drift_count(const oec_drift_fit_t *f) {
    return f ? f->count : 0;
}

oec_status_t oec_drift_fit(const oec_drift_fit_t *f_const, double *out_a, double *out_b) {
    if (!f_const) return OEC_E_INVALID_ARG;
    if (f_const->count < 2) return OEC_E_PARSE;
    oec_drift_fit_t *f = (oec_drift_fit_t *)f_const;

    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    int n = f->count;
    for (int i = 0; i < n; ++i) {
        sx  += f->sample[i];
        sy  += f->qpc[i];
        sxx += f->sample[i] * f->sample[i];
        sxy += f->sample[i] * f->qpc[i];
    }
    double denom = n * sxx - sx * sx;
    if (denom == 0.0) return OEC_E_PARSE;
    double a = (n * sxy - sx * sy) / denom;
    double b = (sy - a * sx) / n;

    double sse = 0.0;
    for (int i = 0; i < n; ++i) {
        double r = f->qpc[i] - (a * f->sample[i] + b);
        sse += r * r;
    }
    f->a = a; f->b = b;
    f->rms = sqrt(sse / n);
    f->fitted = 1;
    if (out_a) *out_a = a;
    if (out_b) *out_b = b;
    return OEC_OK;
}

void oec_drift_reset(oec_drift_fit_t *f) {
    if (!f) return;
    memset(f, 0, sizeof(*f));
}

uint64_t oec_drift_predict_qpc(const oec_drift_fit_t *f, uint64_t sample_index) {
    if (!f || !f->fitted) return 0;
    double v = f->a * (double)sample_index + f->b;
    if (v < 0) return 0;
    return (uint64_t)(v + 0.5);
}

double oec_drift_residual_rms(const oec_drift_fit_t *f) {
    if (!f || !f->fitted) return -1.0;
    return f->rms;
}
