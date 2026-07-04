/**
 * @file main.cpp
 * @brief DrawFusion Game Server — Entry point.
 *
 * Starts the C++ game server:
 *   1. Checks AI service connectivity (gRPC health check)
 *   2. Starts the WebSocket server for real-time game state
 *
 * This is a skeleton — game logic (lobbies, rooms, players) will be
 * added incrementally.
 */

#include <csignal>
#include <iostream>
#include <string>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>
#include <uwebsockets/App.h>

#include "ai_client.h"

using json = nlohmann::json;

// ── Configuration ───────────────────────────────────────────────

constexpr int WS_PORT = 9001;
constexpr const char* AI_SERVICE_ADDR = "localhost:50051";

// ── Signal Handling ─────────────────────────────────────────────

static bool running = true;

void signal_handler(int signum) {
    spdlog::info("Received signal {} — shutting down...", signum);
    running = false;
}

// ── Main ────────────────────────────────────────────────────────

int main() {
    // Setup logging
    auto console = spdlog::stdout_color_mt("server");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("%H:%M:%S │ %^%l%$ │ %v");

    spdlog::info("╔══════════════════════════════════════════════════╗");
    spdlog::info("║        DrawFusion Game Server v1.0.0             ║");
    spdlog::info("╚══════════════════════════════════════════════════╝");

    // Signal handlers
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── AI Service Connection ───────────────────────────────────
    spdlog::info("Connecting to AI service at {}...", AI_SERVICE_ADDR);

    drawfusion::AIClientConfig ai_config;
    ai_config.target_address = AI_SERVICE_ADDR;
    ai_config.deadline_ms = 3000;

    drawfusion::GameMasterClient ai_client(ai_config);

    if (ai_client.IsHealthy()) {
        spdlog::info("✅ AI service is healthy and ready");
    } else {
        spdlog::warn("⚠️  AI service not reachable — server will start without AI");
        spdlog::warn("   Start the Python gRPC server on port 50051 to enable AI features");
    }

    // ── WebSocket Server ────────────────────────────────────────
    spdlog::info("Starting WebSocket server on port {}...", WS_PORT);

    uWS::App()
        .ws<std::string>("/*", {
            // Connection opened
            .open = [](auto* ws) {
                spdlog::info("Player connected: {}", ws->getRemoteAddressAsText());
                json welcome = {
                    {"type", "welcome"},
                    {"message", "Connected to DrawFusion!"},
                    {"version", "1.0.0"}
                };
                ws->send(welcome.dump(), uWS::OpCode::TEXT);
            },

            // Message received
            .message = [&ai_client](auto* ws, std::string_view message, uWS::OpCode opCode) {
                try {
                    auto msg = json::parse(message);
                    std::string type = msg.value("type", "unknown");

                    spdlog::info("Received: type='{}' from {}",
                        type, ws->getRemoteAddressAsText());

                    if (type == "ping") {
                        json pong = {{"type", "pong"}};
                        ws->send(pong.dump(), uWS::OpCode::TEXT);
                    }
                    else if (type == "get_prompt") {
                        // Test the gRPC connection
                        auto result = ai_client.GetPrompt(
                            msg.value("game_id", "default"),
                            msg.value("difficulty", "medium")
                        );
                        if (result) {
                            auto [prompt, category] = *result;
                            json response = {
                                {"type", "prompt"},
                                {"prompt", prompt},
                                {"category", category}
                            };
                            ws->send(response.dump(), uWS::OpCode::TEXT);
                        } else {
                            json err = {
                                {"type", "error"},
                                {"message", "AI service unavailable"}
                            };
                            ws->send(err.dump(), uWS::OpCode::TEXT);
                        }
                    }
                    else {
                        json echo = {
                            {"type", "echo"},
                            {"original", msg}
                        };
                        ws->send(echo.dump(), uWS::OpCode::TEXT);
                    }
                } catch (const json::parse_error& e) {
                    spdlog::warn("Invalid JSON from client: {}", e.what());
                    json err = {{"type", "error"}, {"message", "Invalid JSON"}};
                    ws->send(err.dump(), uWS::OpCode::TEXT);
                }
            },

            // Connection closed
            .close = [](auto* ws, int code, std::string_view message) {
                spdlog::info("Player disconnected: code={}", code);
            }
        })
        .listen(WS_PORT, [](auto* listen_socket) {
            if (listen_socket) {
                spdlog::info("✅ WebSocket server listening on ws://localhost:{}", WS_PORT);
                spdlog::info("");
                spdlog::info("  Server is ready! Connect via WebSocket to test.");
                spdlog::info("  Send {{\"type\":\"ping\"}} for pong");
                spdlog::info("  Send {{\"type\":\"get_prompt\"}} to test AI gRPC");
            } else {
                spdlog::error("❌ Failed to listen on port {}", WS_PORT);
            }
        })
        .run();

    spdlog::info("Server stopped.");
    return 0;
}
