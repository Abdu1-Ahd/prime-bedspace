/*
 * Debug logging helper (NDJSON).
 * Writes to ./debug-7a3ee5.log for this Cursor debug session.
 *
 * NOTE: Do not log secrets/PII.
 */
#ifndef DEBUG_LOG_H
#define DEBUG_LOG_H

#include <stdio.h>
#include <string.h>
#include <sys/time.h>

static inline long long dbg_now_ms(void) {
    struct timeval tv;
    gettimeofday(&tv, NULL);
    return (long long)tv.tv_sec * 1000LL + (long long)(tv.tv_usec / 1000);
}

static inline void dbg_write_ndjson(const char *run_id,
                                    const char *hypothesis_id,
                                    const char *location,
                                    const char *message,
                                    const char *data_json) {
    FILE *f = fopen("debug-7a3ee5.log", "a");
    if (!f) return;

    /* data_json must be a valid JSON object string like {"k":1} */
    if (!data_json || data_json[0] == '\0') data_json = "{}";

    /* Keep message/location short and quote-free */
    if (!run_id) run_id = "pre";
    if (!hypothesis_id) hypothesis_id = "H?";
    if (!location) location = "unknown";
    if (!message) message = "";

    /* Minimal NDJSON payload */
    fprintf(
        f,
        "{\"sessionId\":\"7a3ee5\",\"runId\":\"%s\",\"hypothesisId\":\"%s\","
        "\"location\":\"%s\",\"message\":\"%s\",\"data\":%s,\"timestamp\":%lld}\n",
        run_id, hypothesis_id, location, message, data_json, dbg_now_ms()
    );
    fclose(f);
}

/* Convenience macro for tiny numeric payloads */
#define DBG1(run_id, hyp, loc, msg, k1, v1) do { \
    char _dbg_buf[160]; \
    snprintf(_dbg_buf, sizeof(_dbg_buf), "{\"%s\":%lld}", (k1), (long long)(v1)); \
    dbg_write_ndjson((run_id), (hyp), (loc), (msg), _dbg_buf); \
} while (0)

#define DBG2(run_id, hyp, loc, msg, k1, v1, k2, v2) do { \
    char _dbg_buf[220]; \
    snprintf(_dbg_buf, sizeof(_dbg_buf), "{\"%s\":%lld,\"%s\":%lld}", (k1), (long long)(v1), (k2), (long long)(v2)); \
    dbg_write_ndjson((run_id), (hyp), (loc), (msg), _dbg_buf); \
} while (0)

#endif


// session:9c10a801b
