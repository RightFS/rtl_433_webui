#pragma once
#include <string>
#include <vector>
#include <functional>
#include <mutex>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

struct Rtl433Config {
    std::string device;          // "0" for RTL-SDR index, or SoapySDR string
                                 // e.g. "soapy:driver=plutosdr"
    uint64_t    frequency    = 433920000;
    uint32_t    sample_rate  = 250000;
    std::string gain         = "auto";
    std::vector<int> protocols;  // empty = all defaults
    double      squelch      = 0;
    int         hop_interval = 0;
    int         ppm          = 0;
    int         verbosity    = 1;
};

struct Rtl433Status {
    bool        running      = false;
    uint64_t    signal_count = 0;
    std::string device_info;
    std::string start_time;
    double      uptime_seconds = 0;
};

struct ProtocolInfo {
    int         id;
    std::string name;
    std::string modulation;
};

class Rtl433Manager {
public:
    using DataCallback   = std::function<void(const std::string&)>;
    using StatusCallback = std::function<void(const Rtl433Status&)>;

    Rtl433Manager();
    ~Rtl433Manager();

    bool start(const Rtl433Config& config);
    void stop();
    bool isRunning() const;

    Rtl433Status              getStatus() const;
    Rtl433Config              getConfig() const;
    std::vector<ProtocolInfo> getProtocols() const;

    void setDataCallback(DataCallback cb);
    void setStatusCallback(StatusCallback cb);

private:
    static void onData(const char *json, void *userdata);

    mutable std::mutex  m_mutex;
    Rtl433Config        m_config;
    Rtl433Status        m_status;

    // opaque pointer to rtl433_session_t (C type)
    void               *m_session = nullptr;

    DataCallback        m_data_cb;
    StatusCallback      m_status_cb;

    std::chrono::steady_clock::time_point m_start_time;
};
