#include "rtl433_manager.hpp"
#include <nlohmann/json.hpp>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <signal.h>
#include <fcntl.h>
#include <sstream>
#include <iostream>
#include <cstring>
#include <cstdio>

using json = nlohmann::json;

Rtl433Manager::Rtl433Manager() {
    m_status.running      = false;
    m_status.pid          = -1;
    m_status.signal_count = 0;
}

Rtl433Manager::~Rtl433Manager() {
    stop();
}

std::vector<std::string> Rtl433Manager::buildArgs(const Rtl433Config& cfg) {
    std::vector<std::string> args;
    std::string bin = cfg.rtl433_path.empty() ? "rtl_433" : cfg.rtl433_path;
    args.push_back(bin);

    if (!cfg.device.empty()) {
        args.push_back("-d");
        args.push_back(cfg.device);
    }
    if (cfg.frequency > 0) {
        args.push_back("-f");
        args.push_back(std::to_string(cfg.frequency));
    }
    if (cfg.sample_rate > 0) {
        args.push_back("-s");
        args.push_back(std::to_string(cfg.sample_rate));
    }
    if (!cfg.gain.empty() && cfg.gain != "auto") {
        args.push_back("-g");
        args.push_back(cfg.gain);
    }
    for (int p : cfg.protocols) {
        args.push_back("-R");
        args.push_back(std::to_string(p));
    }
    if (cfg.squelch > 0) {
        args.push_back("-l");
        args.push_back(std::to_string((int)cfg.squelch));
    }
    if (cfg.hop_interval > 0) {
        args.push_back("-H");
        args.push_back(std::to_string(cfg.hop_interval));
    }
    // Output format: JSON with protocol and level metadata
    args.push_back("-F");
    args.push_back("json");
    args.push_back("-M");
    args.push_back("protocol");
    args.push_back("-M");
    args.push_back("level");
    args.push_back("-M");
    args.push_back("time:iso:utc");

    if (!cfg.extra_args.empty()) {
        std::istringstream iss(cfg.extra_args);
        std::string tok;
        while (iss >> tok) args.push_back(tok);
    }
    return args;
}

bool Rtl433Manager::start(const Rtl433Config& config) {
    std::lock_guard<std::mutex> lk(m_mutex);
    if (m_running) return false;

    m_config = config;

    int pipefd[2];
    if (pipe(pipefd) != 0) {
        std::cerr << "pipe() failed: " << strerror(errno) << "\n";
        return false;
    }

    auto args = buildArgs(config);
    std::vector<char*> argv;
    for (auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
    argv.push_back(nullptr);

    pid_t pid = fork();
    if (pid < 0) {
        std::cerr << "fork() failed: " << strerror(errno) << "\n";
        close(pipefd[0]); close(pipefd[1]);
        return false;
    }
    if (pid == 0) {
        // Child: redirect stdout+stderr to pipe write end
        close(pipefd[0]);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        close(pipefd[1]);
        execvp(argv[0], argv.data());
        _exit(127);
    }

    // Parent
    close(pipefd[1]);
    m_pid        = pid;
    m_stdout_fd  = pipefd[0];
    m_running    = true;
    m_start_time = std::chrono::steady_clock::now();

    m_status.running    = true;
    m_status.pid        = pid;
    m_status.signal_count = 0;

    m_reader_thread = std::thread(&Rtl433Manager::readerThread, this);
    return true;
}

void Rtl433Manager::stop() {
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (!m_running) return;
        m_running = false;
        if (m_pid > 0) {
            kill(m_pid, SIGTERM);
            // Give it 2 seconds then SIGKILL
            int status;
            for (int i = 0; i < 20; ++i) {
                usleep(100000);
                if (waitpid(m_pid, &status, WNOHANG) != 0) { m_pid = -1; break; }
            }
            if (m_pid > 0) {
                kill(m_pid, SIGKILL);
                waitpid(m_pid, &status, 0);
                m_pid = -1;
            }
        }
        if (m_stdout_fd >= 0) {
            close(m_stdout_fd);
            m_stdout_fd = -1;
        }
        m_status.running = false;
        m_status.pid     = -1;
    }
    if (m_reader_thread.joinable()) m_reader_thread.join();
}

bool Rtl433Manager::isRunning() const {
    return m_running.load();
}

void Rtl433Manager::readerThread() {
    char buf[4096];
    std::string line_buf;
    while (m_running) {
        int fd;
        { std::lock_guard<std::mutex> lk(m_mutex); fd = m_stdout_fd; }
        if (fd < 0) break;

        ssize_t n = read(fd, buf, sizeof(buf) - 1);
        if (n <= 0) break;
        buf[n] = '\0';
        line_buf += buf;

        size_t pos;
        while ((pos = line_buf.find('\n')) != std::string::npos) {
            std::string line = line_buf.substr(0, pos);
            line_buf = line_buf.substr(pos + 1);
            if (line.empty()) continue;

            // Try to parse as JSON
            try {
                auto j = json::parse(line);
                {
                    std::lock_guard<std::mutex> lk(m_mutex);
                    m_status.signal_count++;
                }
                if (m_data_cb) m_data_cb(line);
            } catch (...) {
                // Not JSON (e.g. status/info lines) - still forward as log
                if (m_data_cb) {
                    json log_msg;
                    log_msg["type"]    = "log";
                    log_msg["message"] = line;
                    m_data_cb(log_msg.dump());
                }
            }
        }
    }

    // Process exited
    m_running = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        m_status.running = false;
        if (m_pid > 0) {
            int status;
            waitpid(m_pid, &status, WNOHANG);
            m_pid = -1;
            m_status.pid = -1;
        }
    }
    if (m_status_cb) {
        m_status_cb(getStatus());
    }
}

Rtl433Status Rtl433Manager::getStatus() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    Rtl433Status s = m_status;
    if (m_running) {
        auto now  = std::chrono::steady_clock::now();
        s.uptime_seconds =
            std::chrono::duration<double>(now - m_start_time).count();
    } else {
        s.uptime_seconds = 0;
    }
    return s;
}

Rtl433Config Rtl433Manager::getConfig() const {
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_config;
}

std::vector<ProtocolInfo> Rtl433Manager::getProtocols() {
    std::vector<ProtocolInfo> protocols;
    std::string bin = m_config.rtl433_path.empty() ? "rtl_433" : m_config.rtl433_path;
    std::string cmd = bin + " -R help 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return protocols;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        std::string s(line);
        // Lines look like: "    [  1] Silvercrest Remote Control"
        // or:              "  [  1] Silvercrest Remote Control ..."
        size_t lb = s.find('[');
        size_t rb = s.find(']');
        if (lb != std::string::npos && rb != std::string::npos && rb > lb) {
            std::string num_str = s.substr(lb + 1, rb - lb - 1);
            // strip spaces
            while (!num_str.empty() && num_str.front() == ' ') num_str.erase(num_str.begin());
            while (!num_str.empty() && num_str.back()  == ' ') num_str.pop_back();
            try {
                int id = std::stoi(num_str);
                std::string name = s.substr(rb + 1);
                while (!name.empty() && (name.front() == ' ' || name.front() == ':'))
                    name.erase(name.begin());
                while (!name.empty() && (name.back() == '\n' || name.back() == '\r' || name.back() == ' '))
                    name.pop_back();
                ProtocolInfo p;
                p.id   = id;
                p.name = name;
                protocols.push_back(p);
            } catch (...) {}
        }
    }
    pclose(fp);
    return protocols;
}

std::vector<DeviceInfo> Rtl433Manager::getDevices() {
    std::vector<DeviceInfo> devices;
    std::string bin = m_config.rtl433_path.empty() ? "rtl_433" : m_config.rtl433_path;
    std::string cmd = bin + " -d list 2>&1";
    FILE* fp = popen(cmd.c_str(), "r");
    if (!fp) return devices;

    char line[512];
    while (fgets(line, sizeof(line), fp)) {
        std::string s(line);
        // Lines vary; look for patterns like "  0:  Realtek, RTL2838UHIDIR, SN: 00000001"
        if (s.find(':') != std::string::npos && !s.empty() && std::isdigit((unsigned char)s.front())) {
            DeviceInfo d;
            std::istringstream iss(s);
            std::string idx_str;
            std::getline(iss, idx_str, ':');
            d.index = idx_str;
            std::getline(iss, d.name);
            while (!d.name.empty() && (d.name.front() == ' '))
                d.name.erase(d.name.begin());
            while (!d.name.empty() && (d.name.back() == '\n' || d.name.back() == '\r'))
                d.name.pop_back();
            devices.push_back(d);
        }
        // SoapySDR devices
        if (s.find("driver=") != std::string::npos) {
            DeviceInfo d;
            d.driver = "soapysdr";
            // extract driver value
            auto dpos = s.find("driver=");
            d.index = s.substr(dpos);
            while (!d.index.empty() && (d.index.back() == '\n' || d.index.back() == '\r' || d.index.back() == ' '))
                d.index.pop_back();
            d.name = d.index;
            devices.push_back(d);
        }
    }
    pclose(fp);
    return devices;
}

void Rtl433Manager::setDataCallback(DataCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_data_cb = std::move(cb);
}

void Rtl433Manager::setStatusCallback(StatusCallback cb) {
    std::lock_guard<std::mutex> lk(m_mutex);
    m_status_cb = std::move(cb);
}
