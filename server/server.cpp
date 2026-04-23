#include "server.hpp"
#include <crow.h>
#include <nlohmann/json.hpp>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

using json = nlohmann::json;

WebServer::WebServer(std::shared_ptr<Rtl433Manager> mgr, int port)
    : m_mgr(std::move(mgr)), m_port(port) {
    setupRoutes();
}

void WebServer::run() {
    m_app.port(m_port).multithreaded().run();
}

void WebServer::stop() {
    m_app.stop();
}

void WebServer::broadcastMessage(const std::string& msg) {
    std::lock_guard<std::mutex> lk(m_ws_mutex);
    for (auto* conn : m_ws_clients) {
        conn->send_text(msg);
    }
}

static std::string guessMime(const std::string& path) {
    if (path.ends_with(".html")) return "text/html";
    if (path.ends_with(".js"))   return "application/javascript";
    if (path.ends_with(".css"))  return "text/css";
    if (path.ends_with(".json")) return "application/json";
    if (path.ends_with(".svg"))  return "image/svg+xml";
    if (path.ends_with(".png"))  return "image/png";
    if (path.ends_with(".ico"))  return "image/x-icon";
    if (path.ends_with(".woff")) return "font/woff";
    if (path.ends_with(".woff2"))return "font/woff2";
    if (path.ends_with(".ttf"))  return "font/ttf";
    return "application/octet-stream";
}

crow::response WebServer::serveStatic(const std::string& req_path) {
    std::string lookup = req_path;
    if (lookup.empty() || lookup == "/") lookup = "/index.html";

    for (size_t i = 0; i < g_embedded_files_count; ++i) {
        const auto& ef = g_embedded_files[i];
        std::string ep(ef.path);
        if (ep == lookup || ep == "/" + lookup) {
            crow::response res(200);
            res.set_header("Content-Type", ef.mime_type);
            res.write(std::string(reinterpret_cast<const char*>(ef.data), ef.size));
            return res;
        }
    }
    // Fallback to index.html for SPA routing
    for (size_t i = 0; i < g_embedded_files_count; ++i) {
        const auto& ef = g_embedded_files[i];
        std::string ep(ef.path);
        if (ep == "/index.html") {
            crow::response res(200);
            res.set_header("Content-Type", "text/html");
            res.write(std::string(reinterpret_cast<const char*>(ef.data), ef.size));
            return res;
        }
    }
    return crow::response(404, "Not Found");
}

nlohmann::json WebServer::configToJson(const Rtl433Config& cfg) {
    json j;
    j["device"]       = cfg.device;
    j["frequency"]    = cfg.frequency;
    j["sample_rate"]  = cfg.sample_rate;
    j["gain"]         = cfg.gain;
    j["protocols"]    = cfg.protocols;
    j["squelch"]      = cfg.squelch;
    j["hop_interval"] = cfg.hop_interval;
    j["rtl433_path"]  = cfg.rtl433_path;
    j["extra_args"]   = cfg.extra_args;
    return j;
}

Rtl433Config WebServer::jsonToConfig(const nlohmann::json& j, const Rtl433Config& current) {
    Rtl433Config cfg = current;
    if (j.contains("device"))       cfg.device       = j["device"].get<std::string>();
    if (j.contains("frequency"))    cfg.frequency    = j["frequency"].get<uint64_t>();
    if (j.contains("sample_rate"))  cfg.sample_rate  = j["sample_rate"].get<uint32_t>();
    if (j.contains("gain"))         cfg.gain         = j["gain"].get<std::string>();
    if (j.contains("protocols"))    cfg.protocols    = j["protocols"].get<std::vector<int>>();
    if (j.contains("squelch"))      cfg.squelch      = j["squelch"].get<double>();
    if (j.contains("hop_interval")) cfg.hop_interval = j["hop_interval"].get<int>();
    if (j.contains("rtl433_path"))  cfg.rtl433_path  = j["rtl433_path"].get<std::string>();
    if (j.contains("extra_args"))   cfg.extra_args   = j["extra_args"].get<std::string>();
    return cfg;
}

void WebServer::setupRoutes() {
    // CORS helper
    auto cors = [](crow::response& res) {
        res.set_header("Access-Control-Allow-Origin",  "*");
        res.set_header("Access-Control-Allow-Methods", "GET,POST,PUT,DELETE,OPTIONS");
        res.set_header("Access-Control-Allow-Headers", "Content-Type,Authorization");
    };

    // OPTIONS preflight
    CROW_ROUTE(m_app, "/api/<path>").methods(crow::HTTPMethod::OPTIONS)
    ([cors](const crow::request&, crow::response& res, const std::string&) {
        cors(res);
        res.code = 204;
        res.end();
    });

    // GET /api/status
    CROW_ROUTE(m_app, "/api/status").methods(crow::HTTPMethod::GET)
    ([this, cors](const crow::request&, crow::response& res) {
        cors(res);
        auto s = m_mgr->getStatus();
        json j;
        j["running"]       = s.running;
        j["pid"]           = s.pid;
        j["signal_count"]  = s.signal_count;
        j["uptime"]        = s.uptime_seconds;
        j["device_info"]   = s.device_info;
        res.set_header("Content-Type", "application/json");
        res.write(j.dump());
        res.end();
    });

    // POST /api/start
    CROW_ROUTE(m_app, "/api/start").methods(crow::HTTPMethod::POST)
    ([this, cors](const crow::request& req, crow::response& res) {
        cors(res);
        if (m_mgr->isRunning()) {
            res.code = 400;
            res.write(R"({"error":"already running"})");
            res.end();
            return;
        }
        Rtl433Config cfg = m_mgr->getConfig();
        if (!req.body.empty()) {
            try {
                auto j = json::parse(req.body);
                cfg = jsonToConfig(j, cfg);
            } catch (const std::exception& e) {
                res.code = 400;
                res.write(std::string(R"({"error":"invalid json: ")") + e.what() + "\"}");
                res.end();
                return;
            }
        }
        bool ok = m_mgr->start(cfg);
        json j;
        j["success"] = ok;
        if (!ok) {
            res.code = 500;
            j["error"] = "failed to start rtl_433";
        }
        res.set_header("Content-Type", "application/json");
        res.write(j.dump());
        res.end();

        // Broadcast status update
        json status_msg;
        status_msg["type"]    = "status";
        status_msg["running"] = ok;
        broadcastMessage(status_msg.dump());
    });

    // POST /api/stop
    CROW_ROUTE(m_app, "/api/stop").methods(crow::HTTPMethod::POST)
    ([this, cors](const crow::request&, crow::response& res) {
        cors(res);
        m_mgr->stop();
        json j;
        j["success"] = true;
        res.set_header("Content-Type", "application/json");
        res.write(j.dump());
        res.end();

        json status_msg;
        status_msg["type"]    = "status";
        status_msg["running"] = false;
        broadcastMessage(status_msg.dump());
    });

    // GET /api/config
    CROW_ROUTE(m_app, "/api/config").methods(crow::HTTPMethod::GET)
    ([this, cors](const crow::request&, crow::response& res) {
        cors(res);
        auto cfg = m_mgr->getConfig();
        res.set_header("Content-Type", "application/json");
        res.write(configToJson(cfg).dump());
        res.end();
    });

    // POST /api/config
    CROW_ROUTE(m_app, "/api/config").methods(crow::HTTPMethod::POST)
    ([this, cors](const crow::request& req, crow::response& res) {
        cors(res);
        try {
            auto j   = json::parse(req.body);
            auto cfg = jsonToConfig(j, m_mgr->getConfig());
            // Store updated config (start uses it later)
            json resp;
            resp["success"] = true;
            resp["config"]  = configToJson(cfg);
            res.set_header("Content-Type", "application/json");
            res.write(resp.dump());
        } catch (const std::exception& e) {
            res.code = 400;
            res.write(std::string(R"({"error":")")  + e.what() + "\"}");
        }
        res.end();
    });

    // GET /api/protocols
    CROW_ROUTE(m_app, "/api/protocols").methods(crow::HTTPMethod::GET)
    ([this, cors](const crow::request&, crow::response& res) {
        cors(res);
        auto protos = m_mgr->getProtocols();
        json arr = json::array();
        for (auto& p : protos) {
            json item;
            item["id"]   = p.id;
            item["name"] = p.name;
            arr.push_back(item);
        }
        res.set_header("Content-Type", "application/json");
        res.write(arr.dump());
        res.end();
    });

    // GET /api/devices
    CROW_ROUTE(m_app, "/api/devices").methods(crow::HTTPMethod::GET)
    ([this, cors](const crow::request&, crow::response& res) {
        cors(res);
        auto devs = m_mgr->getDevices();
        json arr  = json::array();
        for (auto& d : devs) {
            json item;
            item["index"]  = d.index;
            item["name"]   = d.name;
            item["serial"] = d.serial;
            item["driver"] = d.driver;
            arr.push_back(item);
        }
        res.set_header("Content-Type", "application/json");
        res.write(arr.dump());
        res.end();
    });

    // WebSocket /ws
    CROW_WEBSOCKET_ROUTE(m_app, "/ws")
        .onopen([this](crow::websocket::connection& conn) {
            std::lock_guard<std::mutex> lk(m_ws_mutex);
            m_ws_clients.push_back(&conn);
            // Send current status on connect
            auto s = m_mgr->getStatus();
            json j;
            j["type"]         = "status";
            j["running"]      = s.running;
            j["signal_count"] = s.signal_count;
            j["uptime"]       = s.uptime_seconds;
            conn.send_text(j.dump());
        })
        .onclose([this](crow::websocket::connection& conn, const std::string&) {
            std::lock_guard<std::mutex> lk(m_ws_mutex);
            m_ws_clients.erase(
                std::remove(m_ws_clients.begin(), m_ws_clients.end(), &conn),
                m_ws_clients.end());
        })
        .onmessage([this](crow::websocket::connection& conn, const std::string& data, bool) {
            // Handle RPC commands from client
            try {
                auto j = json::parse(data);
                std::string cmd = j.value("cmd", "");
                if (cmd == "start") {
                    Rtl433Config cfg = m_mgr->getConfig();
                    if (j.contains("config")) cfg = jsonToConfig(j["config"], cfg);
                    bool ok = m_mgr->start(cfg);
                    json resp;
                    resp["type"]    = "rpc_result";
                    resp["cmd"]     = "start";
                    resp["success"] = ok;
                    conn.send_text(resp.dump());
                } else if (cmd == "stop") {
                    m_mgr->stop();
                    json resp;
                    resp["type"]    = "rpc_result";
                    resp["cmd"]     = "stop";
                    resp["success"] = true;
                    conn.send_text(resp.dump());
                } else if (cmd == "get_status") {
                    auto s = m_mgr->getStatus();
                    json resp;
                    resp["type"]         = "status";
                    resp["running"]      = s.running;
                    resp["signal_count"] = s.signal_count;
                    resp["uptime"]       = s.uptime_seconds;
                    conn.send_text(resp.dump());
                } else if (cmd == "configure") {
                    json resp;
                    resp["type"]    = "rpc_result";
                    resp["cmd"]     = "configure";
                    resp["success"] = true;
                    conn.send_text(resp.dump());
                }
            } catch (...) {}
        });

    // Static files - SPA catch-all
    CROW_ROUTE(m_app, "/<path>")
    ([this](const std::string& path) -> crow::response {
        return serveStatic("/" + path);
    });

    CROW_ROUTE(m_app, "/")
    ([this]() -> crow::response {
        return serveStatic("/index.html");
    });
}
