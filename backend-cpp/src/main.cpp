/**
 * @file main.cpp
 * @brief DrawFusion Game Server — Auth & Lobby Management.
 */

#include <csignal>
#include <iostream>
#include <string>
#include <random>
#include <chrono>

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <nlohmann/json.hpp>
#include <uwebsockets/App.h>
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

#include "ai_client.h"
#include "db_manager.h"

using json = nlohmann::json;

// ── Configuration ───────────────────────────────────────────────

constexpr int WS_PORT = 9001;
constexpr const char* AI_SERVICE_ADDR = "localhost:50051";

// Will be loaded from environment variables in main()
std::string JWT_SECRET;

// ── Socket State ────────────────────────────────────────────────

struct PerSocketData {
    std::string player_id;
    std::string username;
    std::string lobby_code;
    bool is_authenticated = false;
    std::chrono::steady_clock::time_point last_action_time;
    std::chrono::steady_clock::time_point last_submit_time;
};

// ── Helpers ─────────────────────────────────────────────────────

std::string generate_lobby_code() {
    const char charset[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";
    std::random_device rd;
    std::mt19937 gen(rd());
    std::uniform_int_distribution<> dis(0, sizeof(charset) - 2);
    std::string code;
    for (int i = 0; i < 6; ++i) {
        code += charset[dis(gen)];
    }
    return code;
}

std::string get_future_datetime(int hours) {
    auto now = std::chrono::system_clock::now() + std::chrono::hours(hours);
    std::time_t time = std::chrono::system_clock::to_time_t(now);
    char buf[100];
    std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", std::gmtime(&time));
    return std::string(buf);
}

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

    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);

    // ── Load Environment Variables ──────────────────────────────
    if (const char* env_secret = std::getenv("JWT_SECRET")) {
        JWT_SECRET = env_secret;
        spdlog::info("🔒 JWT_SECRET loaded from environment");
    } else {
        JWT_SECRET = "drawfusion_super_secret_key_123";
        spdlog::warn("⚠️  JWT_SECRET not found in environment! Using insecure default.");
    }

    // ── AI Service Connection ───────────────────────────────────
    drawfusion::AIClientConfig ai_config;
    ai_config.target_address = AI_SERVICE_ADDR;
    drawfusion::GameMasterClient ai_client(ai_config);
    if (ai_client.IsHealthy()) {
        spdlog::info("✅ AI service is healthy and ready");
    } else {
        spdlog::warn("⚠️  AI service not reachable — server will start without AI");
    }

    // ── Database Connection ─────────────────────────────────────
    drawfusion::DatabaseManager db("drawfusion.db");
    if (db.IsOpen()) {
        spdlog::info("✅ Database initialized successfully");
        db.Vacuum(); // Clean up disk space on startup
    } else {
        spdlog::error("❌ Failed to open database");
        return 1;
    }

    // ── Server App ──────────────────────────────────────────────
    spdlog::info("Starting server on port {}...", WS_PORT);

    uWS::App app;

    // ── HTTP Routes (Auth) ──────────────────────────────────────
    
    app.post("/api/register", [&db](auto* res, auto* req) {
        res->onAborted([]() {});
        std::string buffer;
        res->onData([res, buffer = std::move(buffer), &db](std::string_view data, bool last) mutable {
            buffer.append(data.data(), data.length());
            if (last) {
                try {
                    auto payload = json::parse(buffer);
                    std::string username = payload.at("username");
                    std::string email = payload.at("email");
                    std::string password = payload.at("password"); // In prod, hash this first!

                    // System capacity limit (portfolio safety constraint)
                    if (db.GetUserCount() >= 12) {
                        res->writeStatus("403 Forbidden")->end(json{{"error", "System capacity reached (Max 12 users)"}}.dump());
                        return;
                    }

                    if (db.UserExists(email)) {
                        res->writeStatus("400 Bad Request")->end(json{{"error", "Email already exists"}}.dump());
                        return;
                    }

                    auto user_id = db.CreateUser(username, email, password);
                    if (user_id) {
                        res->writeStatus("201 Created")->end(json{{"message", "Registered successfully"}}.dump());
                    } else {
                        res->writeStatus("500 Internal Server Error")->end(json{{"error", "DB Error"}}.dump());
                    }
                } catch (const std::exception& e) {
                    res->writeStatus("400 Bad Request")->end(json{{"error", "Invalid JSON payload"}}.dump());
                }
            }
        });
    });

    app.post("/api/login", [&db](auto* res, auto* req) {
        res->onAborted([]() {});
        std::string buffer;
        res->onData([res, buffer = std::move(buffer), &db](std::string_view data, bool last) mutable {
            buffer.append(data.data(), data.length());
            if (last) {
                try {
                    auto payload = json::parse(buffer);
                    std::string email = payload.at("email");
                    std::string password = payload.at("password");

                    auto user = db.GetUserByEmail(email);
                    if (user && user->password_hash == password) {
                        // Generate JWT token
                        auto token = jwt::create<jwt::traits::nlohmann_json>()
                            .set_issuer("drawfusion")
                            .set_subject(user->id)
                            .set_payload_claim("username", user->username)
                            .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
                            .sign(jwt::algorithm::hs256{JWT_SECRET});

                        res->end(json{{"token", token}, {"username", user->username}, {"id", user->id}}.dump());
                    } else {
                        res->writeStatus("401 Unauthorized")->end(json{{"error", "Invalid credentials"}}.dump());
                    }
                } catch (const std::exception& e) {
                    res->writeStatus("400 Bad Request")->end(json{{"error", "Invalid JSON payload"}}.dump());
                }
            }
        });
    });

    // ── WebSocket Routes (Game) ─────────────────────────────────

    app.ws<PerSocketData>("/*", {
        .open = [](auto* ws) {
            auto* data = ws->getUserData();
            // Initialize timestamps way in the past so first actions are always allowed
            data->last_action_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
            data->last_submit_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
            spdlog::info("Socket connected: {}", ws->getRemoteAddressAsText());
        },

        .message = [&db, &ai_client](auto* ws, std::string_view message, uWS::OpCode opCode) {
            auto* data = ws->getUserData();
            
            try {
                auto msg = json::parse(message);
                std::string type = msg.value("type", "");

                // 1. AUTHENTICATE
                if (type == "authenticate") {
                    std::string token = msg.value("token", "");
                    try {
                        auto decoded = jwt::decode<jwt::traits::nlohmann_json>(token);
                        auto verifier = jwt::verify<jwt::traits::nlohmann_json>()
                            .allow_algorithm(jwt::algorithm::hs256{JWT_SECRET})
                            .with_issuer("drawfusion");
                        verifier.verify(decoded);

                        data->player_id = decoded.get_subject();
                        data->username = decoded.get_payload_claim("username").as_string();
                        data->is_authenticated = true;

                        ws->send(json{{"type", "auth_success"}, {"username", data->username}}.dump(), uWS::OpCode::TEXT);
                        spdlog::info("Player authenticated: {}", data->username);
                    } catch (const std::exception& e) {
                        ws->send(json{{"type", "error"}, {"message", "Auth failed"}}.dump(), uWS::OpCode::TEXT);
                    }
                    return;
                }

                // REQUIRE AUTH FOR ALL OTHER ROUTES
                if (!data->is_authenticated) {
                    ws->send(json{{"type", "error"}, {"message", "Not authenticated"}}.dump(), uWS::OpCode::TEXT);
                    return;
                }

                // GLOBAL RATE LIMIT (Prevent generic spam - max 1 action per second)
                auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration_cast<std::chrono::seconds>(now - data->last_action_time).count() < 1) {
                    ws->send(json{{"type", "error"}, {"message", "Rate limit exceeded. Please wait a moment."}}.dump(), uWS::OpCode::TEXT);
                    return;
                }
                data->last_action_time = now;

                // 2. CREATE LOBBY
                if (type == "create_lobby") {
                    // System capacity limit (portfolio safety constraint)
                    if (db.GetActiveLobbyCount() >= 3) {
                        ws->send(json{{"type", "error"}, {"message", "System capacity reached (Max 3 active lobbies)"}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }

                    std::string code = generate_lobby_code();
                    std::string expires = get_future_datetime(1); // Expires in 1 hour
                    
                    auto lobby_id = db.CreateLobby(data->player_id, code, 4, expires);
                    if (lobby_id) {
                        db.AddPlayerToLobby(*lobby_id, data->player_id);
                        
                        data->lobby_code = code;
                        std::string topic = "lobby:" + code;
                        ws->subscribe(topic);
                        
                        ws->send(json{{"type", "lobby_created"}, {"code", code}}.dump(), uWS::OpCode::TEXT);
                        spdlog::info("Lobby {} created by {}", code, data->username);
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "DB Error creating lobby"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 3. JOIN LOBBY
                else if (type == "join_lobby") {
                    std::string code = msg.value("code", "");
                    auto lobby = db.GetLobbyByCode(code);
                    
                    if (lobby && lobby->status == "waiting") {
                        if (db.AddPlayerToLobby(lobby->id, data->player_id)) {
                            data->lobby_code = code;
                            std::string topic = "lobby:" + code;
                            ws->subscribe(topic);
                            
                            // Publish to everyone else in the lobby
                            ws->publish(topic, json{
                                {"type", "player_joined"},
                                {"username", data->username}
                            }.dump(), uWS::OpCode::TEXT, false);

                            ws->send(json{{"type", "lobby_joined"}, {"code", code}}.dump(), uWS::OpCode::TEXT);
                            spdlog::info("Player {} joined lobby {}", data->username, code);
                        } else {
                            ws->send(json{{"type", "error"}, {"message", "Failed to join (duplicate or full)"}}.dump(), uWS::OpCode::TEXT);
                        }
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "Lobby not found or already started"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 4. GET PROMPT (AI)
                else if (type == "get_prompt") {
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
                        ws->send(json{{"type", "error"}, {"message", "AI service unavailable"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 5. SUBMIT DRAWING (AI BATCH SCORING)
                else if (type == "submit_drawing") {
                    // STRICT 60-SECOND COOLDOWN FOR SUBMISSIONS
                    auto now_submit = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now_submit - data->last_submit_time).count() < 60) {
                        ws->send(json{{"type", "error"}, {"message", "You can only submit once per round (60 second cooldown)."}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }
                    data->last_submit_time = now_submit;

                    spdlog::info("Received submit_drawing, starting async scoring...");
                    
                    // Tell client we are judging
                    json status_msg = {{"type", "scoring_in_progress"}};
                    ws->send(status_msg.dump(), uWS::OpCode::TEXT);

                    // Mock batch of submissions
                    std::vector<std::pair<std::string, std::string>> submissions = {
                        {"player-1", "mock_base64_data_1"},
                        {"player-2", "mock_base64_data_2"}
                    };

                    // Call async without blocking this WebSocket thread
                    ai_client.JudgeRoundAsync(
                        "round-001",
                        "A cat on a skateboard",
                        submissions,
                        [](std::optional<std::map<std::string, drawfusion::GameMasterClient::PlayerScore>> results) {
                            if (results) {
                                spdlog::info("✅ Async scoring complete! Got {} results.", results->size());
                                for (const auto& [pid, score] : *results) {
                                    spdlog::info("   - Player: {}, Score: {:.2f}, Rank: {}", 
                                        pid, score.score, score.rank);
                                }
                            } else {
                                spdlog::error("❌ Async scoring failed.");
                            }
                        }
                    );
                }

            } catch (const json::parse_error& e) {
                ws->send(json{{"type", "error"}, {"message", "Invalid JSON"}}.dump(), uWS::OpCode::TEXT);
            }
        },

        .close = [](auto* ws, int code, std::string_view message) {
            auto* data = ws->getUserData();
            if (data->is_authenticated) {
                spdlog::info("Player disconnected: {}", data->username);
                if (!data->lobby_code.empty()) {
                    // Notify lobby they left
                    std::string topic = "lobby:" + data->lobby_code;
                    ws->publish(topic, json{
                        {"type", "player_left"},
                        {"username", data->username}
                    }.dump(), uWS::OpCode::TEXT, false);
                }
            }
        }
    });

    app.listen(WS_PORT, [](auto* listen_socket) {
        if (listen_socket) {
            spdlog::info("✅ Server listening on port {}", WS_PORT);
            spdlog::info("   HTTP: http://localhost:{}", WS_PORT);
            spdlog::info("   WS:   ws://localhost:{}", WS_PORT);
        } else {
            spdlog::error("❌ Failed to listen on port {}", WS_PORT);
        }
    }).run();

    spdlog::info("Server stopped.");
    return 0;
}
