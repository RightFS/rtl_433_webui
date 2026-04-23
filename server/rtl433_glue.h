/**
 * rtl433_glue.h
 *
 * C API that links rtl_433 as an in-process static library (r_433).
 * The SDR acquisition and signal decoding all run inside our process –
 * no subprocess, no stdin/stdout pipes.
 *
 * Usage:
 *   rtl433_session_t *s = rtl433_session_create();
 *   rtl433_session_set_device(s, "0");
 *   rtl433_session_set_frequency(s, 433920000);
 *   rtl433_session_set_data_callback(s, my_cb, my_userdata);
 *   rtl433_session_start(s);
 *   // ... later ...
 *   rtl433_session_stop(s);
 *   rtl433_session_destroy(s);
 */

#ifndef RTL433_GLUE_H
#define RTL433_GLUE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stddef.h>

/* Opaque handle */
typedef struct rtl433_session rtl433_session_t;

/**
 * Callback invoked (from the event-loop thread) for every decoded signal.
 * @param json  Null-terminated JSON string, e.g. {"model":"Acurite-592TXR",...}
 * @param userdata  Pointer passed to rtl433_session_set_data_callback()
 */
typedef void (*rtl433_data_fn)(const char *json, void *userdata);

/* ── lifecycle ─────────────────────────────────────────────────────────── */

/** Allocate a new session (does not open SDR). */
rtl433_session_t *rtl433_session_create(void);

/** Destroy a session; stops it first if running. */
void rtl433_session_destroy(rtl433_session_t *s);

/* ── configuration (call before start) ────────────────────────────────── */

/** RTL-SDR device index ("0","1",...) or SoapySDR device string
 *  e.g. "soapy:driver=plutosdr", "soapy:driver=rtlsdr" */
void rtl433_session_set_device(rtl433_session_t *s, const char *dev_query);

/** Add a center frequency in Hz (first call sets the primary frequency). */
void rtl433_session_add_frequency(rtl433_session_t *s, uint32_t freq_hz);

/** Clear all previously added frequencies. */
void rtl433_session_clear_frequencies(rtl433_session_t *s);

/** Sample rate in samples/second (default 250000). */
void rtl433_session_set_sample_rate(rtl433_session_t *s, uint32_t rate);

/** Gain: NULL/"auto" for automatic, or numeric string (tenths dB for RTL-SDR,
 *  "LNA=40,VGA=20,AMP=0" for SoapySDR). */
void rtl433_session_set_gain(rtl433_session_t *s, const char *gain_str);

/** PPM frequency correction (default 0). */
void rtl433_session_set_ppm(rtl433_session_t *s, int ppm);

/** Squelch offset in dB (default 0 = disabled). */
void rtl433_session_set_squelch(rtl433_session_t *s, float level_db);

/** Frequency hop interval in seconds (0 = disabled). */
void rtl433_session_set_hop_time(rtl433_session_t *s, int secs);

/** Verbosity: 0=errors only, 1=warnings, 2=info, 3=debug */
void rtl433_session_set_verbosity(rtl433_session_t *s, int verbosity);

/**
 * Protocol selection.
 * By default all protocols registered as default are enabled.
 * To select specific ones: call clear_protocols() then enable_protocol() for each.
 */
void rtl433_session_enable_all_protocols(rtl433_session_t *s);
void rtl433_session_clear_protocols(rtl433_session_t *s);
void rtl433_session_enable_protocol(rtl433_session_t *s, int proto_id);
void rtl433_session_disable_protocol(rtl433_session_t *s, int proto_id);

/** Register callback for decoded data events (JSON). */
void rtl433_session_set_data_callback(rtl433_session_t *s,
                                      rtl433_data_fn cb, void *userdata);

/* ── runtime ───────────────────────────────────────────────────────────── */

/** Start SDR acquisition in a background thread (non-blocking).
 *  Returns 0 on success, negative on error. */
int rtl433_session_start(rtl433_session_t *s);

/** Stop acquisition; blocks until the background thread exits. */
void rtl433_session_stop(rtl433_session_t *s);

/** Returns non-zero if acquisition is currently running. */
int rtl433_session_is_running(rtl433_session_t *s);

/** Device info string (JSON), valid after start(). May be NULL. */
const char *rtl433_session_device_info(rtl433_session_t *s);

/* ── protocol enumeration (no session needed) ──────────────────────────── */

/** Number of known protocol decoders. */
uint32_t rtl433_num_protocols(void);

/** Protocol number (1-based, as shown in rtl_433 -R list). */
int rtl433_protocol_id(uint32_t idx);

/** Human-readable protocol name. */
const char *rtl433_protocol_name(uint32_t idx);

/** Modulation type string ("OOK", "FSK", ...). */
const char *rtl433_protocol_modulation(uint32_t idx);

/** rtl_433 version string. */
const char *rtl433_version_string(void);

#ifdef __cplusplus
}
#endif

#endif /* RTL433_GLUE_H */
