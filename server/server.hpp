#pragma once
#include <crow.h>
#include <nlohmann/json.hpp>
#include <memory>
#include <string>
#include <mutex>
#include <vector>
#include <functional>
#include "rtl433_manager.hpp"

// Forward declarations for embedded files
struct EmbeddedFile {
    const char* path;
    const char* mime_type;
    const unsigned char* data;
    size_t size;
};
extern const EmbeddedFile g_embedded_files[];
extern const size_t       g_embedded_files_count;

class WebServer {
public:
    explicit WebServer(std::shared_ptr<Rtl433Manager> mgr, int port = 8080);
    void run();
    void stop();
    void broadcastMessage(const std::string& msg);

private:
    void setupRoutes();
    crow::response serveStatic(const std::string& path);
    nlohmann::json configToJson(const Rtl433Config& cfg);
    Rtl433Config   jsonToConfig(const nlohmann::json& j, const Rtl433Config& current);

    std::shared_ptr<Rtl433Manager> m_mgr;
    crow::SimpleApp                m_app;
    int                            m_port;

    std::mutex                                       m_ws_mutex;
    std::vector<crow::websocket::connection*>        m_ws_clients;
};
