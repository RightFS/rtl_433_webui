#include "rtl433_manager.hpp"
#include "rtl433_glue.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

using json = nlohmann::json;

/* ── Callback bridge ─────────────────────────────────────────────────────── */

void Rtl433Manager::onData(const char *json_str, void *userdata)
{
    auto *self = static_cast<Rtl433Manager*>(userdata);
    if (!json_str) return;

    {
        std::lock_guard<std::mutex> lk(self->m_mutex);
        self->m_status.signal_count++;
        if (self->m_session)
            self->m_status.device_info =
                rtl433_session_device_info(
                    static_cast<rtl433_session_t*>(self->m_session)) ?: "";
    }

    if (self->m_data_cb)
        self->m_data_cb(json_str);
}

/* ── Lifecycle ───────────────────────────────────────────────────────────── */

Rtl433Manager::Rtl433Manager()  = default;
Rtl433Manager::~Rtl433Manager() { stop(); }

bool Rtl433Manager::start(const Rtl433Config& config)
{
    stop();

    auto *s = rtl433_session_create();
    if (!s) return false;

    if (!config.device.empty())
        rtl433_session_set_device(s, config.device.c_str());

    if (config.frequency > 0)
        rtl433_session_add_frequency(s, static_cast<uint32_t>(config.frequency));

    if (config.sample_rate > 0)
        rtl433_session_set_sample_rate(s, config.sample_rate);

    rtl433_session_set_gain(s, config.gain.empty() ? "auto" : config.gain.c_str());

    if (config.ppm != 0)
        rtl433_session_set_ppm(s, config.ppm);

    if (config.squelch > 0)
        rtl433_session_set_squelch(s, static_cast<float>(config.squelch));

    if (config.hop_interval > 0)
        rtl433_session_set_hop_time(s, config.hop_interval);

    rtl433_session_set_verbosity(s, config.verbosity);

    if (!config.protocols.empty()) {
        rtl433_session_clear_protocols(s);
        for (int id : config.protocols)
            rtl433_session_enable_protocol(s, id);
    } else {
        rtl433_session_enable_all_protocols(s);
    }

    rtl433_session_set_data_callback(s, &Rtl433Manager::onData, this);

    if (rtl433_session_start(s) != 0) {
        rtl433_session_destroy(s);
        return false;
    }

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_session = s;
        m_config  = config;
        m_status.running      = true;
        m_status.signal_count = 0;
        m_status.device_info  = "";

        auto now_t = std::time(nullptr);
        std::ostringstream oss;
        oss << std::put_time(std::localtime(&now_t), "%Y-%m-%dT%H:%M:%S");
        m_status.start_time = oss.str();
        m_start_time = std::chrono::steady_clock::now();
    }
    return true;
}

void Rtl433Manager::stop()
{
    rtl433_session_t *s = nullptr;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        s = static_cast<rtl433_session_t*>(m_session);
        m_session       = nullptr;
        m_status.running = false;
    }
    if (s) {
        rtl433_session_stop(s);
        rtl433_session_destroy(s);
    }
}

bool Rtl433Manager::isRunning() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    if (!m_session) return false;
    return rtl433_session_is_running(
               static_cast<rtl433_session_t*>(m_session)) != 0;
}

Rtl433Status Rtl433Manager::getStatus() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    Rtl433Status st = m_status;
    if (st.running) {
        auto now = std::chrono::steady_clock::now();
        st.uptime_seconds =
            std::chrono::duration<double>(now - m_start_time).count();
    }
    return st;
}

Rtl433Config Rtl433Manager::getConfig() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_config;
}

std::vector<ProtocolInfo> Rtl433Manager::getProtocols() const
{
    uint32_t n = rtl433_num_protocols();
    std::vector<ProtocolInfo> result;
    result.reserve(n);
    for (uint32_t i = 0; i < n; ++i) {
        ProtocolInfo p;
        p.id         = rtl433_protocol_id(i);
        p.name       = rtl433_protocol_name(i);
        p.modulation = rtl433_protocol_modulation(i);
        result.push_back(std::move(p));
    }
    return result;
}

void Rtl433Manager::setDataCallback(DataCallback cb)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_data_cb = std::move(cb);
}

void Rtl433Manager::setStatusCallback(StatusCallback cb)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_status_cb = std::move(cb);
}
