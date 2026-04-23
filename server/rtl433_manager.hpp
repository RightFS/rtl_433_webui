#pragma once
#include <string>
#include <vector>
#include <functional>
#include <thread>
#include <mutex>
#include <atomic>
#include <chrono>
#include <nlohmann/json.hpp>

struct Rtl433Config {
    std::string device;          // e.g. "0" for RTL-SDR, "driver=plutosdr" for PlutoSDR
    uint64_t    frequency;       // center frequency in Hz, e.g. 433920000
    uint32_t    sample_rate;     // samples/sec, e.g. 250000
    std::string gain;            // "auto" or numeric string
    std::vector<int> protocols;  // empty = all, otherwise specific protocol IDs
    double      squelch;         // squelch level dB, 0 = disabled
    int         hop_interval;    // frequency hop interval in seconds, 0 = disabled
    std::string rtl433_path;     // path to rtl_433 binary
    std::string extra_args;      // additional command-line arguments
};

struct Rtl433Status {
    bool        running;
    int         pid;
    uint64_t    signal_count;
    std::string device_info;
    std::string start_time;
    double      uptime_seconds;
};

struct ProtocolInfo {
    int         id;
    std::string name;
    std::string modulation;
};

struct DeviceInfo {
    std::string index;
    std::string name;
    std::string serial;
    std::string driver;
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

    Rtl433Status          getStatus() const;
    Rtl433Config          getConfig() const;
    std::vector<ProtocolInfo> getProtocols();
    std::vector<DeviceInfo>   getDevices();

    void setDataCallback(DataCallback cb);
    void setStatusCallback(StatusCallback cb);

private:
    void readerThread();
    std::vector<std::string> buildArgs(const Rtl433Config& cfg);

    mutable std::mutex  m_mutex;
    Rtl433Config        m_config;
    Rtl433Status        m_status;

    std::atomic<bool>   m_running{false};
    pid_t               m_pid{-1};
    int                 m_stdout_fd{-1};

    std::thread         m_reader_thread;
    DataCallback        m_data_cb;
    StatusCallback      m_status_cb;

    std::chrono::steady_clock::time_point m_start_time;
};
