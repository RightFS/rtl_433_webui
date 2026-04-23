/**
 * rtl433_glue.c
 *
 * In-process integration of rtl_433 as a static library (r_433).
 *
 * Strategy
 * --------
 * rtl_433's main() in rtl_433.c contains several static functions that drive
 * the SDR data pipeline:
 *   - reset_sdr_callback() – reset demodulator state
 *   - sdr_callback()       – process a raw IQ buffer through all demodulators
 *   - acquire_callback()   – SDR thread → mg_broadcast → event-loop thread
 *   - sdr_handler()        – event-loop thread receives broadcast, calls sdr_callback
 *   - timer_handler()      – watchdog + frequency hopping
 *   - start_sdr()          – opens and starts SDR device
 *
 * All the functions they call (baseband, pulse-detect, decoders, …) are
 * compiled into the r_433 static library.  We just copy the driver glue here
 * (without `static`), so we can call them from our own main loop – without
 * modifying the rtl_433 submodule at all.
 *
 * Output
 * ------
 * We register a custom data_output_t that serialises every decoded event to
 * JSON via rtl_433's own data_output_json_create() + open_memstream(), then
 * invokes the user-supplied rtl433_data_fn callback.
 *
 * Thread model
 * ------------
 *   Main thread         – Crow HTTP/WebSocket server
 *   Event-loop thread   – mg_mgr_poll() loop; sdr_callback runs here
 *   SDR acq thread      – started by librtlsdr/SoapySDR inside sdr_start();
 *                         sends IQ blocks via mg_broadcast → event-loop thread
 */

/* ── Includes ────────────────────────────────────────────────────────────── */
#define _GNU_SOURCE   /* open_memstream, strdup */

#include "rtl433_glue.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>
#include <pthread.h>
#include <time.h>
#include <math.h>
#include <signal.h>

/* rtl_433 public API */
#include "r_api.h"
#include "r_device.h"
#include "rtl_433.h"          /* r_cfg_t, constants */
#include "rtl_433_devices.h"  /* DEVICES macro, rtl_433_devices[] */

/* rtl_433 internal headers (same includes as rtl_433.c) */
#include "r_private.h"        /* dm_state */
#include "sdr.h"              /* sdr_open, sdr_start, … */
#include "baseband.h"         /* baseband_low_pass_filter, demod_FM, … */
#include "pulse_detect.h"     /* pulse_detect_package, pulse_detect_reset, … */
#include "pulse_detect_fsk.h"
#include "pulse_data.h"
#include "pulse_slicer.h"     /* PULSE_DATA_OOK, PULSE_DATA_FSK */
#include "pulse_analyzer.h"
#include "am_analyze.h"
#include "samp_grab.h"
#include "raw_output.h"
#include "data.h"
#include "data_tag.h"
#include "output_file.h"
#include "output_log.h"
#include "r_util.h"
#include "compat_time.h"
#include "logger.h"
#include "fatal.h"
#include "list.h"
#include "abuf.h"
#include "fileformat.h"
#include "mongoose.h"

/* ── Custom data_output_t ────────────────────────────────────────────────── */

typedef struct {
    data_output_t  base;      /* MUST be first */
    rtl433_data_fn cb;
    void          *userdata;
} custom_output_t;

static void R_API_CALLCONV custom_print(data_output_t *output, data_t *data)
{
    custom_output_t *co = (custom_output_t *)output;
    if (!co->cb || !data) return;

    char  *buf = NULL;
    size_t len = 0;
    FILE  *mf  = open_memstream(&buf, &len);
    if (!mf) return;

    /*
     * data_output_json_create() stores the FILE* and its free function
     * calls fclose() on it (if != stdout).  So after data_output_free()
     * the memory stream is flushed/closed and buf is valid.
     */
    data_output_t *json = data_output_json_create(0, mf);
    if (json) {
        data_output_print(json, data);
        data_output_free(json); /* also closes mf */
    } else {
        fclose(mf);
    }

    if (buf && len > 0) {
        /* Strip leading/trailing whitespace (the JSON printer adds newlines) */
        char *p = buf;
        while (*p == '\n' || *p == '\r' || *p == ' ') p++;
        size_t n = strlen(p);
        while (n > 0 && (p[n-1] == '\n' || p[n-1] == '\r' || p[n-1] == ' '))
            p[--n] = '\0';
        if (n > 0)
            co->cb(p, co->userdata);
    }
    free(buf);
}

static void R_API_CALLCONV custom_start(data_output_t *output,
                                        char const *const *fields, int num)
{
    (void)output; (void)fields; (void)num;
}

static void R_API_CALLCONV custom_free(data_output_t *output)
{
    free(output);
}

static data_output_t *custom_output_create(rtl433_data_fn cb, void *userdata)
{
    custom_output_t *co = calloc(1, sizeof(*co));
    if (!co) return NULL;
    co->base.output_start = custom_start;
    co->base.output_print = custom_print;
    co->base.output_free  = custom_free;
    co->cb       = cb;
    co->userdata = userdata;
    return &co->base;
}

/* ── SDR callback chain (adapted from rtl_433.c static functions) ────────── */

/*
 * reset_sdr_callback  – mirrors the static version in rtl_433.c
 */
static void reset_sdr_callback(r_cfg_t *cfg)
{
    struct dm_state *demod = cfg->demod;

    get_time_now(&demod->now);

    demod->frame_start_ago   = 0;
    demod->frame_end_ago     = 0;
    demod->frame_event_count = 0;
    demod->frame_quality     = 0;

    demod->min_level_auto = 0.0f;
    demod->noise_level    = 0.0f;

    baseband_low_pass_filter_reset(&demod->lowpass_filter_state);
    baseband_demod_FM_reset(&demod->demod_FM_state);

    pulse_detect_reset(demod->pulse_detect);
}

/*
 * sdr_callback  – processes one IQ buffer through all demodulators.
 * Mirrors the static function in rtl_433.c.
 */
static void sdr_callback(unsigned char *iq_buf, uint32_t len, void *ctx)
{
    r_cfg_t *cfg   = ctx;
    struct dm_state *demod = cfg->demod;
    char time_str[LOCAL_TIME_BUFLEN];
    unsigned long n_samples;

    if (!demod) return;

    for (void **iter = cfg->raw_handler.elems; iter && *iter; ++iter) {
        raw_output_t *output = *iter;
        raw_output_frame(output, iq_buf, len);
    }

    if ((cfg->bytes_to_read > 0) && (cfg->bytes_to_read <= len)) {
        len = cfg->bytes_to_read;
        cfg->exit_async = 1;
    }

    time_t last_frame_sec = demod->now.tv_sec;
    get_time_now(&demod->now);

    n_samples = len / demod->sample_size;
    if (n_samples * demod->sample_size != len)
        print_log(LOG_WARNING, __func__, "Sample buffer not aligned to sample size!");
    if (!n_samples) {
        print_log(LOG_WARNING, __func__, "Sample buffer too short!");
        return;
    }

    if (demod->frame_start_ago) demod->frame_start_ago += n_samples;
    if (demod->frame_end_ago)   demod->frame_end_ago   += n_samples;

    cfg->watchdog++;

    if (demod->samp_grab)
        samp_grab_push(demod->samp_grab, iq_buf, len);

    /* AM demodulation */
    float avg_db;
    if (demod->sample_size == 2) {
        if (demod->use_mag_est)
            avg_db = magnitude_est_cu8(iq_buf, demod->buf.temp, n_samples);
        else
            avg_db = envelope_detect(iq_buf, demod->buf.temp, n_samples);
    } else {
        avg_db = magnitude_est_cs16((int16_t *)iq_buf, demod->buf.temp, n_samples);
    }

    if (demod->min_level_auto == 0.0f) demod->min_level_auto = demod->min_level;
    if (demod->noise_level    == 0.0f) demod->noise_level    = demod->min_level_auto - 3.0f;

    int noise_only   = avg_db < demod->noise_level + 3.0f;
    int process_frame = demod->squelch_offset <= 0 || !noise_only
        || demod->load_info.format || demod->analyze_pulses
        || demod->dumper.len || demod->samp_grab;

    cfg->total_frames_count   += 1;
    if (noise_only) {
        cfg->total_frames_squelch += 1;
        demod->noise_level = (demod->noise_level * 7 + avg_db) / 8;
        if (demod->auto_level > 0 && demod->noise_level < demod->min_level - 3.0f
                && fabsf(demod->min_level_auto - demod->noise_level - 3.0f) > 1.0f) {
            demod->min_level_auto = demod->noise_level + 3.0f;
            print_logf(LOG_WARNING, "Auto Level",
                       "Noise %.1f dB, adjusting min level to %.1f dB",
                       demod->noise_level, demod->min_level_auto);
            pulse_detect_set_levels(demod->pulse_detect, demod->use_mag_est,
                                    demod->level_limit, demod->min_level_auto,
                                    demod->min_snr, demod->detect_verbosity);
        }
    } else {
        demod->noise_level = (demod->noise_level * 31 + avg_db) / 32;
    }

    if (cfg->report_noise && last_frame_sec != demod->now.tv_sec
            && demod->now.tv_sec % cfg->report_noise == 0) {
        print_logf(LOG_WARNING, "Auto Level",
                   "Current %s level %.1f dB, estimated noise %.1f dB",
                   noise_only ? "noise" : "signal", avg_db, demod->noise_level);
    }

    if (process_frame)
        baseband_low_pass_filter(&demod->lowpass_filter_state,
                                 demod->buf.temp, demod->am_buf, n_samples);

    /* FM demodulation */
    unsigned fpdm = cfg->fsk_pulse_detect_mode;
    if (cfg->fsk_pulse_detect_mode == FSK_PULSE_DETECT_AUTO) {
        fpdm = (cfg->frequency[cfg->frequency_index] > FSK_PULSE_DETECTOR_LIMIT)
               ? FSK_PULSE_DETECT_NEW : FSK_PULSE_DETECT_OLD;
    }

    if (demod->enable_FM_demod && process_frame) {
        float low_pass = demod->low_pass != 0.0f ? demod->low_pass : fpdm ? 0.2f : 0.1f;
        if (demod->sample_size == 2)
            baseband_demod_FM(&demod->demod_FM_state, iq_buf, demod->buf.fm,
                              n_samples, cfg->samp_rate, low_pass);
        else
            baseband_demod_FM_cs16(&demod->demod_FM_state, (int16_t *)iq_buf,
                                   demod->buf.fm, n_samples, cfg->samp_rate, low_pass);
    }

    /* Pulse detection and decoding */
    int d_events = 0;
    if (demod->r_devs.len || demod->analyze_pulses || demod->dumper.len || demod->samp_grab) {
        int package_type = PULSE_DATA_OOK;
        while (package_type && process_frame) {
            int p_events = 0;
            package_type = pulse_detect_package(demod->pulse_detect,
                                                demod->am_buf, demod->buf.fm,
                                                n_samples, cfg->samp_rate,
                                                cfg->input_pos,
                                                &demod->pulse_data,
                                                &demod->fsk_pulse_data, fpdm);
            if (package_type) {
                if (!demod->frame_start_ago)
                    demod->frame_start_ago = demod->pulse_data.start_ago;
                demod->frame_end_ago = demod->pulse_data.end_ago;
            }
            if (package_type == PULSE_DATA_OOK) {
                calc_rssi_snr(cfg, &demod->pulse_data);
                if (demod->analyze_pulses)
                    fprintf(stderr, "Detected OOK package\t%s\n",
                            time_pos_str(cfg, demod->pulse_data.start_ago, time_str));
                p_events += run_ook_demods(&demod->r_devs, &demod->pulse_data);
                cfg->total_frames_ook += 1;
                cfg->total_frames_events += p_events > 0;
                cfg->frames_ook  += 1;
                cfg->frames_events += p_events > 0;

                if (cfg->verbosity >= LOG_TRACE) pulse_data_print(&demod->pulse_data);
                if (cfg->raw_mode == 1
                        || (cfg->raw_mode == 2 && p_events == 0)
                        || (cfg->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses
                        && (cfg->grab_mode <= 1
                            || (cfg->grab_mode == 2 && p_events == 0)
                            || (cfg->grab_mode == 3 && p_events > 0))) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    pulse_analyzer(&demod->pulse_data, package_type, &device);
                }

            } else if (package_type == PULSE_DATA_FSK) {
                calc_rssi_snr(cfg, &demod->fsk_pulse_data);
                if (demod->analyze_pulses)
                    fprintf(stderr, "Detected FSK package\t%s\n",
                            time_pos_str(cfg, demod->fsk_pulse_data.start_ago, time_str));
                p_events += run_fsk_demods(&demod->r_devs, &demod->fsk_pulse_data);
                cfg->total_frames_fsk  += 1;
                cfg->total_frames_events += p_events > 0;
                cfg->frames_fsk    += 1;
                cfg->frames_events += p_events > 0;

                if (cfg->verbosity >= LOG_TRACE) pulse_data_print(&demod->fsk_pulse_data);
                if (cfg->raw_mode == 1
                        || (cfg->raw_mode == 2 && p_events == 0)
                        || (cfg->raw_mode == 3 && p_events > 0)) {
                    data_t *data = pulse_data_print_data(&demod->fsk_pulse_data);
                    event_occurred_handler(cfg, data);
                }
                if (demod->analyze_pulses
                        && (cfg->grab_mode <= 1
                            || (cfg->grab_mode == 2 && p_events == 0)
                            || (cfg->grab_mode == 3 && p_events > 0))) {
                    r_device device = {.log_fn = log_device_handler, .output_ctx = cfg};
                    pulse_analyzer(&demod->fsk_pulse_data, package_type, &device);
                }
            }
            d_events += p_events;
        }

        demod->frame_event_count += d_events;

        if (demod->frame_start_ago && demod->frame_end_ago > n_samples) {
            if (demod->samp_grab) {
                if (cfg->grab_mode == 1
                        || (cfg->grab_mode == 2 && demod->frame_event_count == 0)
                        || (cfg->grab_mode == 3 && demod->frame_event_count > 0)) {
                    unsigned frame_pad    = n_samples / 8;
                    unsigned start_padded = demod->frame_start_ago + frame_pad;
                    unsigned end_padded   = demod->frame_end_ago - frame_pad;
                    unsigned len_padded   = start_padded - end_padded;
                    samp_grab_write(demod->samp_grab, len_padded, end_padded);
                }
            }
            demod->frame_start_ago   = 0;
            demod->frame_event_count = 0;
            demod->frame_quality     = 0;
        }
    }

    if (demod->am_analyze)
        am_analyze(demod->am_analyze, demod->am_buf, n_samples,
                   cfg->verbosity >= LOG_INFO, NULL);

    cfg->input_pos += n_samples;
    if (cfg->bytes_to_read > 0) cfg->bytes_to_read -= len;

    if (cfg->after_successful_events_flag && d_events > 0) {
        if (cfg->after_successful_events_flag == 1)
            cfg->exit_async = 1;
        else
            cfg->hop_now = 1;
    }

    /* Frequency hop */
    time_t rawtime;
    time(&rawtime);
    int hop_index = cfg->hop_times > cfg->frequency_index
                    ? cfg->frequency_index : cfg->hop_times - 1;
    if (cfg->hop_times > 0 && cfg->frequencies > 1
            && difftime(rawtime, cfg->hop_start_time) >= cfg->hop_time[hop_index]) {
        cfg->hop_now = 1;
    }

    if (cfg->duration > 0 && rawtime >= cfg->stop_time) {
        cfg->exit_async = 1;
        print_log(LOG_CRITICAL, __func__, "Time expired, exiting!");
    }

    if (cfg->stats_now || (cfg->report_stats && cfg->stats_interval
            && rawtime >= cfg->stats_time)) {
        event_occurred_handler(cfg,
            create_report_data(cfg, cfg->stats_now ? 3 : cfg->report_stats));
        flush_report_data(cfg);
        if (rawtime >= cfg->stats_time)
            cfg->stats_time += cfg->stats_interval;
        if (cfg->stats_now) cfg->stats_now--;
    }

    if (cfg->hop_now && !cfg->exit_async) {
        cfg->hop_now = 0;
        time(&cfg->hop_start_time);
        cfg->frequency_index = (cfg->frequency_index + 1) % cfg->frequencies;
        sdr_set_center_freq(cfg->dev, cfg->frequency[cfg->frequency_index], 1);
    }
}

/* ── Mongoose event handlers ─────────────────────────────────────────────── */

/* Forward declaration */
static void glue_timer_handler(struct mg_connection *nc, int ev, void *ev_data);

static void glue_sdr_handler(struct mg_connection *nc, int ev_type, void *ev_data)
{
    if (nc->sock != INVALID_SOCKET || ev_type != MG_EV_POLL) return;
    if (nc->handler != glue_timer_handler)                    return;

    r_cfg_t *cfg    = nc->user_data;
    sdr_event_t *ev = ev_data;

    data_t *data = NULL;
    if (ev->ev & SDR_EV_RATE)
        data = data_int(data, "sample_rate", "", NULL, (int)ev->sample_rate);
    if (ev->ev & SDR_EV_CORR)
        data = data_int(data, "freq_correction", "", NULL, ev->freq_correction);
    if (ev->ev & SDR_EV_FREQ) {
        data = data_int(data, "center_frequency", "", NULL, (int)ev->center_frequency);
        if (cfg->frequencies > 1) {
            data = data_ary(data, "frequencies", "", NULL,
                            data_array(cfg->frequencies, DATA_INT, cfg->frequency));
            data = data_ary(data, "hop_times", "", NULL,
                            data_array(cfg->hop_times, DATA_INT, cfg->hop_time));
        }
    }
    if (ev->ev & SDR_EV_GAIN)
        data = data_str(data, "gain", "", NULL, ev->gain_str);
    if (data)
        event_occurred_handler(cfg, data);

    if (ev->ev == SDR_EV_DATA) {
        cfg->samp_rate        = ev->sample_rate;
        cfg->center_frequency = ev->center_frequency;
        sdr_callback((unsigned char *)ev->buf, ev->len, cfg);
    }

    if (cfg->exit_async) {
        sdr_stop(cfg->dev);
        cfg->exit_async++;
    }
}

static void glue_acquire_callback(sdr_event_t *ev, void *ctx)
{
    struct mg_mgr *mgr = ctx;
    mg_broadcast(mgr, glue_sdr_handler, (void *)ev, sizeof(*ev));
}

static int glue_start_sdr(r_cfg_t *cfg)
{
    int r;
    if (cfg->dev) {
        r = sdr_close(cfg->dev);
        cfg->dev = NULL;
        if (r < 0)
            print_logf(LOG_ERROR, "Input", "Closing SDR failed (%d)", r);
    }
    r = sdr_open(&cfg->dev, cfg->dev_query, cfg->verbosity);
    if (r < 0) return -1;

    cfg->dev_info            = sdr_get_dev_info(cfg->dev);
    cfg->demod->sample_size  = sdr_get_sample_size(cfg->dev);

    sdr_set_sample_rate(cfg->dev, cfg->samp_rate, 1);
    sdr_apply_settings(cfg->dev, cfg->settings_str, 1);
    sdr_set_tuner_gain(cfg->dev, cfg->gain_str, 1);

    if (cfg->ppm_error)
        sdr_set_freq_correction(cfg->dev, cfg->ppm_error, 1);

    r = sdr_reset(cfg->dev, cfg->verbosity);
    if (r < 0) print_log(LOG_ERROR, "Input", "Failed to reset buffers.");

    sdr_activate(cfg->dev);
    sdr_set_center_freq(cfg->dev, cfg->center_frequency, 1);

    r = sdr_start(cfg->dev, glue_acquire_callback, (void *)get_mgr(cfg),
                  DEFAULT_ASYNC_BUF_NUMBER, cfg->out_block_size);
    if (r < 0)
        print_logf(LOG_ERROR, "Input", "async start failed (%d).", r);

    cfg->dev_state = DEVICE_STATE_STARTING;
    return r;
}

static void glue_timer_handler(struct mg_connection *nc, int ev, void *ev_data)
{
    r_cfg_t *cfg = (r_cfg_t *)nc->user_data;
    switch (ev) {
    case MG_EV_TIMER: {
        double now  = *(double *)ev_data;
        (void)now;
        double next = mg_time() + 1.5;
        mg_set_timer(nc, next);

        if (cfg->watchdog != 0) {
            if (cfg->dev_state == DEVICE_STATE_STARTING
                    || cfg->dev_state == DEVICE_STATE_GRACE) {
                cfg->dev_state = DEVICE_STATE_STARTED;
                time(&cfg->sdr_since);
            }
            cfg->watchdog = 0;
            break;
        }

        if (cfg->dev_state == DEVICE_STATE_STARTING) {
            cfg->dev_state = DEVICE_STATE_GRACE;
            break;
        }
        if (cfg->dev_state == DEVICE_STATE_GRACE
                || cfg->dev_state == DEVICE_STATE_STARTED) {
            if (cfg->dev_mode == DEVICE_MODE_QUIT) {
                print_log(LOG_ERROR, "Input", "SDR stalled, exiting.");
                cfg->exit_async = 1;
            } else if (cfg->dev_mode == DEVICE_MODE_RESTART) {
                print_log(LOG_WARNING, "Input", "SDR stalled, restarting.");
                glue_start_sdr(cfg);
            } else {
                print_log(LOG_WARNING, "Input", "SDR stalled, pausing.");
                cfg->dev_state = DEVICE_STATE_STOPPED;
                cfg->exit_async = 1;
            }
        }
        break;
    }
    default:
        break;
    }
}

/* ── Session struct ──────────────────────────────────────────────────────── */

struct rtl433_session {
    r_cfg_t        *cfg;
    pthread_t       thread;
    volatile int    running;
    pthread_mutex_t mutex;

    /* pending configuration */
    char           *dev_query;
    char           *gain_str;
    int             ppm;
    uint32_t        frequencies[MAX_FREQS];
    int             num_frequencies;
    uint32_t        sample_rate;
    float           squelch_offset;
    int             hop_time_secs;
    int             verbosity;
    int             use_all_protocols;      /* 1=all defaults, 0=use custom list */
    int             custom_protos[1024];
    int             num_custom_protos;
    int             disabled_protos[1024];
    int             num_disabled_protos;

    rtl433_data_fn  cb;
    void           *cb_userdata;
};

/* ── Background thread ───────────────────────────────────────────────────── */

static void *session_thread(void *arg)
{
    rtl433_session_t *s = arg;
    r_cfg_t *cfg = r_create_cfg();
    if (!cfg) {
        s->running = 0;
        return NULL;
    }
    s->cfg = cfg;

    /* Apply configuration */
    cfg->verbosity = s->verbosity;

    if (s->dev_query)
        cfg->dev_query = strdup(s->dev_query);

    if (s->gain_str)
        cfg->gain_str = strdup(s->gain_str);

    if (s->ppm)
        cfg->ppm_error = s->ppm;

    if (s->sample_rate)
        cfg->samp_rate = s->sample_rate;

    /* Frequencies */
    if (s->num_frequencies > 0) {
        for (int i = 0; i < s->num_frequencies && i < MAX_FREQS; i++)
            cfg->frequency[i] = s->frequencies[i];
        cfg->frequencies = s->num_frequencies;
    } else {
        cfg->frequency[0] = DEFAULT_FREQUENCY;
        cfg->frequencies  = 1;
    }
    cfg->center_frequency = cfg->frequency[0];

    if (s->hop_time_secs > 0 && s->num_frequencies > 1) {
        for (int i = 0; i < cfg->frequencies && i < MAX_FREQS; i++)
            cfg->hop_time[i] = s->hop_time_secs;
        cfg->hop_times = cfg->frequencies;
    }

    /* Squelch */
    if (s->squelch_offset > 0.0f)
        cfg->demod->squelch_offset = s->squelch_offset;

    /* Device mode: MANUAL so a stall doesn't exit but pauses */
    cfg->dev_mode = DEVICE_MODE_MANUAL;

    /* Protocols */
    if (s->use_all_protocols || s->num_custom_protos == 0) {
        register_all_protocols(cfg, 0);
        /* Disable any explicitly disabled ones */
        for (int i = 0; i < s->num_disabled_protos; i++) {
            for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
                r_device *r_dev = *iter;
                if ((int)r_dev->protocol_num == s->disabled_protos[i]) {
                    unregister_protocol(cfg, r_dev);
                    break;
                }
            }
        }
    } else {
        /* Only enable explicitly requested protocols */
        for (int i = 0; i < s->num_custom_protos; i++) {
            int id = s->custom_protos[i];
            for (unsigned j = 0; j < cfg->num_r_devices; j++) {
                if ((int)cfg->devices[j].protocol_num == id) {
                    register_protocol(cfg, &cfg->devices[j], NULL);
                    break;
                }
            }
        }
    }

    /* Enable FM demod if any FSK decoders are registered */
    for (void **iter = cfg->demod->r_devs.elems; iter && *iter; ++iter) {
        r_device *r_dev = *iter;
        if (r_dev->modulation >= FSK_DEMOD_MIN_VAL) {
            cfg->demod->enable_FM_demod = 1;
            break;
        }
    }

    /* Register our custom output handler */
    data_output_t *our_output = custom_output_create(s->cb, s->cb_userdata);
    if (our_output) {
        list_push(&cfg->output_handler, our_output);
        /* Also add a log output so warnings appear on stderr */
        add_log_output(cfg, NULL);
    }

    /* start_outputs initialises each handler */
    char const **well_known = well_known_output_fields(cfg);
    start_outputs(cfg, well_known);
    free((void *)well_known);

    /* Redirect rtl_433 log messages through its own logger */
    r_redirect_logging(cfg);

    if (cfg->out_block_size < MINIMAL_BUF_LENGTH
            || cfg->out_block_size > MAXIMAL_BUF_LENGTH)
        cfg->out_block_size = DEFAULT_BUF_LENGTH;

    pulse_detect_set_levels(cfg->demod->pulse_detect, cfg->demod->use_mag_est,
                            cfg->demod->level_limit, cfg->demod->min_level,
                            cfg->demod->min_snr, cfg->demod->detect_verbosity);
    if (cfg->demod->am_analyze) {
        cfg->demod->am_analyze->frequency  = &cfg->center_frequency;
        cfg->demod->am_analyze->samp_rate  = &cfg->samp_rate;
        cfg->demod->am_analyze->sample_size = &cfg->demod->sample_size;
    }

    cfg->report_time = REPORT_TIME_DATE;

    /* Start SDR */
    int r = glue_start_sdr(cfg);
    if (r < 0) {
        print_log(LOG_ERROR, "rtl433_glue", "Failed to open SDR device.");
        r_free_cfg(cfg);
        s->cfg     = NULL;
        s->running = 0;
        return NULL;
    }

    time(&cfg->hop_start_time);

    /* Add a dummy connection so the timer fires */
    struct mg_add_sock_opts opts = {.user_data = cfg};
    struct mg_connection *nc =
        mg_add_sock_opt(get_mgr(cfg), INVALID_SOCKET, glue_timer_handler, opts);
    mg_set_timer(nc, mg_time() + 2.5);

    /* Event loop */
    while (!cfg->exit_async) {
        mg_mgr_poll(cfg->mgr, 200);
    }

    sdr_stop(cfg->dev);
    r_free_cfg(cfg);
    s->cfg     = NULL;
    s->running = 0;
    return NULL;
}

/* ── Public API ──────────────────────────────────────────────────────────── */

rtl433_session_t *rtl433_session_create(void)
{
    rtl433_session_t *s = calloc(1, sizeof(*s));
    if (!s) return NULL;
    pthread_mutex_init(&s->mutex, NULL);
    s->sample_rate       = DEFAULT_SAMPLE_RATE;
    s->use_all_protocols = 1;
    s->verbosity         = LOG_WARNING;
    return s;
}

void rtl433_session_destroy(rtl433_session_t *s)
{
    if (!s) return;
    rtl433_session_stop(s);
    pthread_mutex_destroy(&s->mutex);
    free(s->dev_query);
    free(s->gain_str);
    free(s);
}

void rtl433_session_set_device(rtl433_session_t *s, const char *dev_query)
{
    if (!s) return;
    free(s->dev_query);
    s->dev_query = dev_query ? strdup(dev_query) : NULL;
}

void rtl433_session_add_frequency(rtl433_session_t *s, uint32_t freq_hz)
{
    if (!s || s->num_frequencies >= MAX_FREQS) return;
    s->frequencies[s->num_frequencies++] = freq_hz;
}

void rtl433_session_clear_frequencies(rtl433_session_t *s)
{
    if (!s) return;
    s->num_frequencies = 0;
}

void rtl433_session_set_sample_rate(rtl433_session_t *s, uint32_t rate)
{
    if (s) s->sample_rate = rate;
}

void rtl433_session_set_gain(rtl433_session_t *s, const char *gain_str)
{
    if (!s) return;
    free(s->gain_str);
    s->gain_str = (gain_str && strcmp(gain_str, "auto") != 0) ? strdup(gain_str) : NULL;
}

void rtl433_session_set_ppm(rtl433_session_t *s, int ppm)
{
    if (s) s->ppm = ppm;
}

void rtl433_session_set_squelch(rtl433_session_t *s, float level_db)
{
    if (s) s->squelch_offset = level_db;
}

void rtl433_session_set_hop_time(rtl433_session_t *s, int secs)
{
    if (s) s->hop_time_secs = secs;
}

void rtl433_session_set_verbosity(rtl433_session_t *s, int verbosity)
{
    if (s) s->verbosity = verbosity;
}

void rtl433_session_enable_all_protocols(rtl433_session_t *s)
{
    if (s) s->use_all_protocols = 1;
}

void rtl433_session_clear_protocols(rtl433_session_t *s)
{
    if (!s) return;
    s->use_all_protocols  = 0;
    s->num_custom_protos  = 0;
    s->num_disabled_protos = 0;
}

void rtl433_session_enable_protocol(rtl433_session_t *s, int proto_id)
{
    if (!s || s->num_custom_protos >= 1024) return;
    s->custom_protos[s->num_custom_protos++] = proto_id;
}

void rtl433_session_disable_protocol(rtl433_session_t *s, int proto_id)
{
    if (!s || s->num_disabled_protos >= 1024) return;
    s->disabled_protos[s->num_disabled_protos++] = proto_id;
}

void rtl433_session_set_data_callback(rtl433_session_t *s,
                                      rtl433_data_fn cb, void *userdata)
{
    if (!s) return;
    s->cb          = cb;
    s->cb_userdata = userdata;
}

int rtl433_session_start(rtl433_session_t *s)
{
    if (!s) return -1;
    pthread_mutex_lock(&s->mutex);
    if (s->running) {
        pthread_mutex_unlock(&s->mutex);
        return 0; /* already running */
    }
    s->running = 1;
    int r = pthread_create(&s->thread, NULL, session_thread, s);
    if (r != 0) {
        s->running = 0;
        pthread_mutex_unlock(&s->mutex);
        return -1;
    }
    pthread_mutex_unlock(&s->mutex);
    return 0;
}

void rtl433_session_stop(rtl433_session_t *s)
{
    if (!s) return;
    pthread_mutex_lock(&s->mutex);
    if (!s->running) {
        pthread_mutex_unlock(&s->mutex);
        return;
    }
    /* Signal the event loop to exit */
    if (s->cfg)
        s->cfg->exit_async = 1;
    pthread_mutex_unlock(&s->mutex);

    pthread_join(s->thread, NULL);
}

int rtl433_session_is_running(rtl433_session_t *s)
{
    return s ? s->running : 0;
}

const char *rtl433_session_device_info(rtl433_session_t *s)
{
    if (!s || !s->cfg) return NULL;
    return s->cfg->dev_info;
}

/* ── Protocol enumeration ────────────────────────────────────────────────── */

/*
 * We need the device list.  r_init_cfg() builds it from the DEVICES macro,
 * so we create a temporary cfg to enumerate it, then cache the result.
 */
static r_device *g_proto_devices  = NULL;
static uint32_t  g_proto_count    = 0;
static pthread_once_t g_proto_once = PTHREAD_ONCE_INIT;

static void init_proto_list(void)
{
    /* Use a throw-away cfg to get the built-in device list */
    r_cfg_t *tmp = r_create_cfg();
    if (!tmp) return;
    g_proto_devices = tmp->devices;   /* steal the pointer */
    g_proto_count   = tmp->num_r_devices;
    tmp->devices    = NULL;           /* prevent r_free_cfg from freeing it */
    r_free_cfg(tmp);
}

uint32_t rtl433_num_protocols(void)
{
    pthread_once(&g_proto_once, init_proto_list);
    return g_proto_count;
}

int rtl433_protocol_id(uint32_t idx)
{
    pthread_once(&g_proto_once, init_proto_list);
    if (idx >= g_proto_count || !g_proto_devices) return -1;
    return (int)g_proto_devices[idx].protocol_num;
}

const char *rtl433_protocol_name(uint32_t idx)
{
    pthread_once(&g_proto_once, init_proto_list);
    if (idx >= g_proto_count || !g_proto_devices) return "";
    return g_proto_devices[idx].name ? g_proto_devices[idx].name : "";
}

const char *rtl433_protocol_modulation(uint32_t idx)
{
    pthread_once(&g_proto_once, init_proto_list);
    if (idx >= g_proto_count || !g_proto_devices) return "";
    int mod = g_proto_devices[idx].modulation;
    if (mod >= FSK_DEMOD_MIN_VAL) return "FSK";
    return "OOK";
}

const char *rtl433_version_string(void)
{
    return version_string();
}
