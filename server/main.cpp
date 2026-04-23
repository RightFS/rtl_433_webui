#include <iostream>
#include <fstream>
#include <memory>
#include <csignal>
#include <nlohmann/json.hpp>
#include "rtl433_manager.hpp"
#include "server.hpp"

using json = nlohmann::json;

static std::shared_ptr<WebServer> g_server;
static std::shared_ptr<Rtl433Manager> g_mgr;

static void signalHandler(int sig) {
    std::cout << "\nReceived signal " << sig << ", shutting down...\n";
    if (g_mgr) g_mgr->stop();
    if (g_server) g_server->stop();
}

static Rtl433Config loadConfig(const std::string& path) {
    Rtl433Config cfg;
    cfg.frequency   = 433920000;
    cfg.sample_rate = 250000;
    cfg.gain        = "auto";
    cfg.squelch     = 0;
    cfg.hop_interval= 0;

    std::ifstream f(path);
    if (!f.is_open()) return cfg;

    try {
        json j = json::parse(f);
        if (j.contains("device"))       cfg.device       = j["device"].get<std::string>();
        if (j.contains("frequency"))    cfg.frequency    = j["frequency"].get<uint64_t>();
        if (j.contains("sample_rate"))  cfg.sample_rate  = j["sample_rate"].get<uint32_t>();
        if (j.contains("gain"))         cfg.gain         = j["gain"].get<std::string>();
        if (j.contains("protocols"))    cfg.protocols    = j["protocols"].get<std::vector<int>>();
        if (j.contains("squelch"))      cfg.squelch      = j["squelch"].get<double>();
        if (j.contains("hop_interval")) cfg.hop_interval = j["hop_interval"].get<int>();
        if (j.contains("rtl433_path"))  cfg.rtl433_path  = j["rtl433_path"].get<std::string>();
        if (j.contains("extra_args"))   cfg.extra_args   = j["extra_args"].get<std::string>();
    } catch (const std::exception& e) {
        std::cerr << "Config parse error: " << e.what() << "\n";
    }
    return cfg;
}

int main(int argc, char* argv[]) {
    std::string config_path = "config.json";
    int port = 8080;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if ((arg == "-c" || arg == "--config") && i + 1 < argc) {
            config_path = argv[++i];
        } else if ((arg == "-p" || arg == "--port") && i + 1 < argc) {
            port = std::stoi(argv[++i]);
        } else if (arg == "-h" || arg == "--help") {
            std::cout << "Usage: " << argv[0]
                      << " [-c config.json] [-p port]\n";
            return 0;
        }
    }

    // Try config.json, then config.example.json
    Rtl433Config cfg = loadConfig(config_path);
    if (cfg.device.empty()) {
        cfg = loadConfig("config.example.json");
    }

    g_mgr = std::make_shared<Rtl433Manager>();
    if (!g_mgr->start(cfg)) {
        std::cerr << "Note: rtl_433 auto-start failed (binary not found or device unavailable).\n"
                  << "Use the web UI to start it manually after configuring the correct path/device.\n";
    }

    g_server = std::make_shared<WebServer>(g_mgr, port);

    // Forward rtl_433 data to all WebSocket clients
    g_mgr->setDataCallback([](const std::string& data) {
        if (g_server) g_server->broadcastMessage(data);
    });

    std::signal(SIGINT,  signalHandler);
    std::signal(SIGTERM, signalHandler);

    std::cout << "rtl_433 WebUI starting on port " << port << "\n";
    g_server->run();
    return 0;
}
