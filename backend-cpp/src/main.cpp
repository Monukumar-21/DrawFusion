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
    bool is_ready = false;
    std::chrono::steady_clock::time_point last_action_time;
    std::chrono::steady_clock::time_point last_submit_time;
};

// ── Lobby Context ───────────────────────────────────────────────

struct LobbyContext {
    std::unordered_map<std::string, bool> player_ready; // player_id -> is_ready
    
    // Round data
    bool round_active = false;
    std::string session_id;
    std::string round_id;
    std::string prompt;
    long long end_time_ms = 0;
    std::vector<std::pair<std::string, std::string>> submissions;
    std::unordered_map<std::string, std::string> sub_ids; // player_id -> db submission id
    bool judging_started = false;
    std::unordered_set<std::string> players_used_hint;
    
    // Peeking mechanism
    std::unordered_map<std::string, std::unordered_set<std::string>> peekers_of; // target_username -> set of peeker_usernames
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

std::string getPlayerIdByUsernameInLobby(drawfusion::DatabaseManager& db, const std::string& lobby_code, const std::string& username) {
    auto lobby = db.GetLobbyByCode(lobby_code);
    if (lobby) {
        auto players = db.GetLobbyPlayers(lobby->id);
        for (const auto& p : players) {
            auto u = db.GetUserById(p.player_id);
            if (u && u->username == username) {
                return p.player_id;
            }
        }
    }
    return "";
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
    
    // Global CORS preflight handler
    app.options("/*", [](auto* res, auto* req) {
        res->writeHeader("Access-Control-Allow-Origin", "*");
        res->writeHeader("Access-Control-Allow-Methods", "GET, POST, OPTIONS");
        res->writeHeader("Access-Control-Allow-Headers", "Content-Type");
        res->end();
    });

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

                    if (db.GetUserCount() >= 12) {
                        res->writeStatus("403 Forbidden");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"error", "System capacity reached (Max 12 users)"}}.dump());
                        return;
                    }

                    if (db.UserExists(email)) {
                        res->writeStatus("400 Bad Request");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"error", "Email already exists"}}.dump());
                        return;
                    }

                    auto user_id = db.CreateUser(username, email, password);
                    if (user_id) {
                        res->writeStatus("201 Created");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"message", "Registered successfully"}}.dump());
                    } else {
                        res->writeStatus("500 Internal Server Error");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"error", "DB Error"}}.dump());
                    }
                } catch (const std::exception& e) {
                    res->writeStatus("400 Bad Request");
                    res->writeHeader("Access-Control-Allow-Origin", "*");
                    res->end(json{{"error", "Invalid JSON payload"}}.dump());
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

                        res->writeStatus("200 OK");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"token", token}, {"username", user->username}, {"id", user->id}}.dump());
                    } else {
                        res->writeStatus("401 Unauthorized");
                        res->writeHeader("Access-Control-Allow-Origin", "*");
                        res->end(json{{"error", "Invalid credentials"}}.dump());
                    }
                } catch (const std::exception& e) {
                    res->writeStatus("400 Bad Request");
                    res->writeHeader("Access-Control-Allow-Origin", "*");
                    res->end(json{{"error", "Invalid JSON payload"}}.dump());
                }
            }
        });
    });

    // ── WebSocket Routes (Game) ─────────────────────────────────

    // Global in-memory map for host API keys: lobby_code -> groq_key
    std::unordered_map<std::string, std::string> lobby_api_keys;
    
    // Global in-memory state for active lobbies
    std::unordered_map<std::string, LobbyContext> active_lobbies;
    
    // Global in-memory set for concurrent login tracking
    std::unordered_set<std::string> active_players;

    // Helper to trigger judging safely
    auto trigger_scoring = [&db, &ai_client, &lobby_api_keys, &active_lobbies](uWS::App* global_app, const std::string& lobby_code) {
        auto& ctx = active_lobbies[lobby_code];
        if (ctx.judging_started || !ctx.round_active) return;
        ctx.judging_started = true;
        
        std::string groq_key = "";
        if (lobby_api_keys.count(lobby_code)) {
            groq_key = lobby_api_keys[lobby_code];
        }
        
        uWS::Loop* loop = uWS::Loop::get();
        std::string topic = "lobby:" + lobby_code;
        
        global_app->publish(topic, json{{"type", "scoring_in_progress"}}.dump(), uWS::OpCode::TEXT, false);
        
        // Make a copy of submissions for async
        auto subs = ctx.submissions;
        auto s_ids = ctx.sub_ids;
        
        ai_client.JudgeRoundAsync(ctx.round_id, ctx.prompt, subs, 
            [loop, global_app, topic, lobby_code, s_ids, &db, &active_lobbies, groq_key](std::optional<std::map<std::string, drawfusion::GameMasterClient::PlayerScore>> results) {
                if (results) {
                    spdlog::info("✅ Async batch scoring complete! Got {} results.", results->size());
                    json res_json = json::array();
                    for (const auto& [pid, score] : *results) {
                        json item;
                        item["player_id"] = pid; // In production map to username, using pid for now
                        item["score"] = score.score;
                        item["rank"] = score.rank;
                        item["feedback"] = score.feedback;
                        res_json.push_back(item);
                        
                        if (s_ids.count(pid)) {
                            db.UpdateSubmissionScore(s_ids.at(pid), score.score, score.feedback, score.confidence);
                        }
                    }
                    json res_payload;
                    res_payload["type"] = "judging_results";
                    res_payload["results"] = res_json;
                    res_payload["free_trial"] = groq_key.empty();
                    std::string payload = res_payload.dump();
                    loop->defer([global_app, topic, payload, lobby_code, &active_lobbies]() {
                        global_app->publish(topic, payload, uWS::OpCode::TEXT, false);
                        active_lobbies[lobby_code].round_active = false;
                        active_lobbies[lobby_code].judging_started = false;
                    });
                } else {
                    spdlog::error("❌ Batch Scoring failed.");
                    std::string payload = json{{"type", "error"}, {"message", "Scoring failed"}}.dump();
                    loop->defer([global_app, topic, payload, lobby_code, &active_lobbies]() {
                        global_app->publish(topic, payload, uWS::OpCode::TEXT, false);
                        active_lobbies[lobby_code].round_active = false;
                        active_lobbies[lobby_code].judging_started = false;
                    });
                }
            }, groq_key);
    };

    // Helper to evaluate and broadcast lobby ready state
    auto evaluate_lobby_ready = [&db, &active_lobbies](uWS::App* global_app, const std::string& code) {
        bool all_ready = true;
        int active_players = 0;
        auto lobby = db.GetLobbyByCode(code);
        if (lobby) {
            auto players = db.GetLobbyPlayers(lobby->id);
            active_players = players.size();
            for (const auto& p : players) {
                if (!active_lobbies[code].player_ready[p.player_id]) {
                    all_ready = false;
                    break;
                }
            }
        }
        std::string payload_ready = json{{"type", (all_ready && active_players >= 2) ? "all_ready" : "not_all_ready"}}.dump();
        global_app->publish("lobby:" + code, payload_ready, uWS::OpCode::TEXT, false);
    };

    app.ws<PerSocketData>("/*", {
        .maxPayloadLength = 16 * 1024 * 1024, // 16MB max payload for large base64 canvas images
        .idleTimeout = 30,
        .open = [](auto* ws) {
            auto* data = ws->getUserData();
            // Initialize timestamps way in the past so first actions are always allowed
            data->last_action_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
            data->last_submit_time = std::chrono::steady_clock::now() - std::chrono::hours(1);
            spdlog::info("Socket connected: {}", ws->getRemoteAddressAsText());
        },

        .message = [&db, &ai_client, global_app = &app, &active_lobbies, &lobby_api_keys, trigger_scoring, &active_players, evaluate_lobby_ready](auto* ws, std::string_view message, uWS::OpCode opCode) {
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
                        
                        // Verify user still exists in DB
                        auto user = db.GetUserById(data->player_id);
                        if (!user) {
                            ws->send(json{{"type", "error"}, {"message", "Account expired. Please log in again."}}.dump(), uWS::OpCode::TEXT);
                            ws->send(json{{"type", "account_deleted"}}.dump(), uWS::OpCode::TEXT);
                            ws->close();
                            return;
                        }
                        
                        // Concurrent Login Prevention
                        if (active_players.count(data->player_id)) {
                            std::string user_topic = "user:" + data->player_id;
                            global_app->publish(user_topic, json{
                                {"type", "security_alert"},
                                {"message", "Security Alert: Someone attempted to log in to your account from another session!"}
                            }.dump(), uWS::OpCode::TEXT, false);
                            
                            ws->send(json{{"type", "error"}, {"message", "Account already logged in elsewhere. Please change your password if this wasn't you."}}.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                        
                        active_players.insert(data->player_id);

                        data->username = decoded.get_payload_claim("username").as_string();
                        data->is_authenticated = true;

                        ws->subscribe("user:" + data->player_id);

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
                if (type != "peek_stream" && type != "start_peek" && type != "stop_peek") {
                    auto now = std::chrono::steady_clock::now();
                    if (std::chrono::duration_cast<std::chrono::seconds>(now - data->last_action_time).count() < 1) {
                        ws->send(json{{"type", "error"}, {"message", "Rate limit exceeded. Please wait a moment."}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }
                    data->last_action_time = now;
                }

                // 2. DELETE ACCOUNT
                if (type == "delete_account") {
                    spdlog::info("Deleting account for player: {}", data->player_id);
                    if (db.DeleteUser(data->player_id)) {
                        ws->send(json{{"type", "account_deleted"}}.dump(), uWS::OpCode::TEXT);
                        ws->close();
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "Failed to delete account"}}.dump(), uWS::OpCode::TEXT);
                    }
                    return;
                }
                
                // 2.5 CHANGE PASSWORD
                if (type == "change_password") {
                    std::string new_password = msg.value("new_password", "");
                    if (new_password.empty()) {
                        ws->send(json{{"type", "error"}, {"message", "Password cannot be empty"}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }
                    if (db.UpdatePassword(data->player_id, new_password)) {
                        ws->send(json{{"type", "password_changed"}}.dump(), uWS::OpCode::TEXT);
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "Failed to change password"}}.dump(), uWS::OpCode::TEXT);
                    }
                    return;
                }

                // 3. CREATE LOBBY
                if (type == "create_lobby") {
                    std::string groq_key = msg.value("groq_key", "");

                    // Validate API keys ONLY if provided (otherwise it's a Free Trial)
                    if (!groq_key.empty()) {
                        auto err = ai_client.ValidateKeys(groq_key);
                        if (err) {
                            ws->send(json{{"type", "error"}, {"message", *err}}.dump(), uWS::OpCode::TEXT);
                            return;
                        }
                    }

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
                        lobby_api_keys[code] = groq_key;
                        
                        data->lobby_code = code;
                        active_lobbies[code].player_ready[data->player_id] = false;

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
                        auto players = db.GetLobbyPlayers(lobby->id);
                        if (players.size() >= lobby->max_players) {
                            ws->send(json{{"type", "error"}, {"message", "Lobby is full"}}.dump(), uWS::OpCode::TEXT);
                            
                            // Send join_attempt_full to the Host
                            std::string host_topic = "user:" + lobby->host_player_id;
                            global_app->publish(host_topic, json{
                                {"type", "join_attempt_full"},
                                {"username", data->username}
                            }.dump(), uWS::OpCode::TEXT, false);
                            
                            return;
                        }

                        if (db.AddPlayerToLobby(lobby->id, data->player_id)) {
                            data->lobby_code = code;
                            active_lobbies[code].player_ready[data->player_id] = false;

                            std::string topic = "lobby:" + code;
                            ws->subscribe(topic);
                            
                            // Publish to everyone else in the lobby
                            ws->publish(topic, json{
                                {"type", "player_joined"},
                                {"username", data->username}
                            }.dump(), uWS::OpCode::TEXT, false);

                            std::vector<std::string> player_names;
                            auto host = db.GetUserById(lobby->host_player_id);
                            if (host) player_names.push_back(host->username);
                            
                            auto players = db.GetLobbyPlayers(lobby->id);
                            for (const auto& p : players) {
                                if (p.player_id != lobby->host_player_id) {
                                    auto u = db.GetUserById(p.player_id);
                                    if (u) player_names.push_back(u->username);
                                }
                            }

                            ws->send(json{
                                {"type", "lobby_joined"}, 
                                {"code", code},
                                {"players", player_names}
                            }.dump(), uWS::OpCode::TEXT);
                            spdlog::info("Player {} joined lobby {}", data->username, code);
                            
                            evaluate_lobby_ready(global_app, code);
                        } else {
                            ws->send(json{{"type", "error"}, {"message", "Failed to join (duplicate or full)"}}.dump(), uWS::OpCode::TEXT);
                        }
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "Lobby not found or already started"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 3.5 LEAVE LOBBY
                else if (type == "leave_lobby") {
                    if (data->lobby_code.empty()) return;
                    
                    std::string code = data->lobby_code;
                    auto lobby = db.GetLobbyByCode(code);
                    if (!lobby) return;

                    std::string topic = "lobby:" + code;
                    
                    if (lobby->host_player_id == data->player_id) {
                        // Host leaves: terminate lobby
                        ws->publish(topic, json{{"type", "lobby_terminated"}}.dump(), uWS::OpCode::TEXT, false);
                        db.DeleteLobby(lobby->id);
                        active_lobbies.erase(code);
                    } else {
                        // Member leaves
                        db.RemovePlayerFromLobby(lobby->id, data->player_id);
                        ws->publish(topic, json{{"type", "player_left"}, {"username", data->username}}.dump(), uWS::OpCode::TEXT, false);
                        evaluate_lobby_ready(global_app, code);
                    }
                    data->lobby_code = "";
                    ws->send(json{{"type", "left_lobby"}}.dump(), uWS::OpCode::TEXT);
                    ws->unsubscribe(topic);
                }

                // 3.6 KICK PLAYER
                else if (type == "kick_player") {
                    if (data->lobby_code.empty()) return;
                    std::string target_username = msg.value("username", "");
                    std::string code = data->lobby_code;
                    
                    auto lobby = db.GetLobbyByCode(code);
                    if (!lobby || lobby->host_player_id != data->player_id) return; // Only host can kick

                    // Find target player ID by username
                    auto players = db.GetLobbyPlayers(lobby->id);
                    std::string target_player_id;
                    for (const auto& p : players) {
                        auto u = db.GetUserById(p.player_id);
                        if (u && u->username == target_username) {
                            target_player_id = p.player_id;
                            break;
                        }
                    }
                    if (target_player_id.empty() || target_player_id == data->player_id) return; // Cant kick self

                    // Remove from DB
                    db.RemovePlayerFromLobby(lobby->id, target_player_id);

                    // Notify the kicked player directly
                    std::string target_topic = "user:" + target_player_id;
                    global_app->publish(target_topic, json{{"type", "kicked"}}.dump(), uWS::OpCode::TEXT, false);

                    // Notify the lobby
                    std::string topic = "lobby:" + code;
                    global_app->publish(topic, json{{"type", "player_left"}, {"username", target_username}}.dump(), uWS::OpCode::TEXT, false);
                    evaluate_lobby_ready(global_app, code);
                }

                // 4. GET PROMPT (AI)
                else if (type == "get_prompt") {
                    if (data->lobby_code.empty()) {
                        ws->send(json{{"type", "error"}, {"message", "Not in a lobby"}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }

                    std::string groq_key = "";
                    if (lobby_api_keys.count(data->lobby_code)) {
                        groq_key = lobby_api_keys[data->lobby_code];
                    }

                    auto result = ai_client.GetPrompt(
                        msg.value("game_id", "default"),
                        msg.value("difficulty", "medium"),
                        {}, groq_key
                    );
                    if (result) {
                        auto [prompt, category] = *result;
                        
                        auto now = std::chrono::system_clock::now();
                        auto end_time = now + std::chrono::seconds(60);
                        long long end_time_ms = std::chrono::duration_cast<std::chrono::milliseconds>(end_time.time_since_epoch()).count();
                        
                        auto& ctx = active_lobbies[data->lobby_code];
                        ctx.round_active = true;
                        ctx.judging_started = false;
                        ctx.prompt = prompt;
                        ctx.end_time_ms = end_time_ms;
                        ctx.round_id = generate_lobby_code() + "-round";
                        ctx.submissions.clear();
                        ctx.sub_ids.clear();
                        ctx.players_used_hint.clear();
                        
                        // Fix DB constraint issue
                        auto lobby = db.GetLobbyByCode(data->lobby_code);
                        if (lobby) {
                            if (ctx.session_id.empty()) {
                                int total_rounds = groq_key.empty() ? 1 : 3;
                                auto sess = db.CreateGameSession(lobby->id, "multiplayer", total_rounds);
                                if (sess) ctx.session_id = *sess;
                            }
                            if (!ctx.session_id.empty()) {
                                db.CreateRound(ctx.session_id, 1, prompt, category, 60);
                            }
                        }

                        // Broadcast to everyone
                        json response = {
                            {"type", "round_started"},
                            {"prompt", prompt},
                            {"category", category},
                            {"end_time_ms", end_time_ms}
                        };
                        std::string topic = "lobby:" + data->lobby_code;
                        std::string payload = response.dump();

                        ws->publish(topic, payload, uWS::OpCode::TEXT, false);
                        ws->send(payload, uWS::OpCode::TEXT);
                        
                        // Unready all players for next round
                        for (auto& [pid, ready] : ctx.player_ready) {
                            ready = false;
                        }
                    } else {
                        ws->send(json{{"type", "error"}, {"message", "AI service unavailable"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 4.5 GET HINT (AI)
                else if (type == "get_hint") {
                    if (data->lobby_code.empty()) return;
                    auto& ctx = active_lobbies[data->lobby_code];
                    
                    if (!ctx.round_active) {
                        ws->send(json{{"type", "error"}, {"message", "No active round!"}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }
                    
                    if (ctx.players_used_hint.count(data->player_id)) {
                        ws->send(json{{"type", "error"}, {"message", "You can only request one hint per round to save API limits!"}}.dump(), uWS::OpCode::TEXT);
                        return;
                    }

                    std::string groq_key = "";
                    if (lobby_api_keys.count(data->lobby_code)) {
                        groq_key = lobby_api_keys[data->lobby_code];
                    }
                    
                    // Mark hint as used
                    ctx.players_used_hint.insert(data->player_id);
                    
                    // Execute synchronous RPC call to AI service to retrieve hint
                    // Future enhancement: Move this to async to prevent blocking the WebSocket loop
                    auto hint_opt = ai_client.GetHint(ctx.round_id, ctx.prompt, 30.0f, 1, groq_key);
                    if (hint_opt) {
                        ws->send(json{{"type", "hint_response"}, {"hint", *hint_opt}}.dump(), uWS::OpCode::TEXT);
                    } else {
                        // Unmark if it failed so they can try again
                        ctx.players_used_hint.erase(data->player_id);
                        ws->send(json{{"type", "error"}, {"message", "Failed to get hint"}}.dump(), uWS::OpCode::TEXT);
                    }
                }

                // 5. SUBMIT DRAWING (BATCH GATHERING)
                else if (type == "submit_drawing") {
                    if (data->lobby_code.empty()) return;
                    auto& ctx = active_lobbies[data->lobby_code];
                    if (!ctx.round_active) return;
                    
                    std::string b64_image = msg.value("image", "");
                    
                    // Save to DB
                    auto sub_id_opt = db.SaveSubmission(ctx.round_id, data->player_id, b64_image);
                    
                    // Add to batch
                    ctx.submissions.push_back({data->player_id, b64_image});
                    if (sub_id_opt) {
                        ctx.sub_ids[data->player_id] = *sub_id_opt;
                    }
                    
                    spdlog::info("Player {} submitted drawing. Total submissions: {}/{}", data->username, ctx.submissions.size(), ctx.player_ready.size());
                    
                    ws->send(json{{"type", "scoring_in_progress"}}.dump(), uWS::OpCode::TEXT);
                    
                    // Trigger scoring if everyone is done
                    if (ctx.submissions.size() >= ctx.player_ready.size()) {
                        trigger_scoring(global_app, data->lobby_code);
                    }
                }
                
                // 5.5 SET READY
                else if (type == "set_ready") {
                    if (data->lobby_code.empty()) return;
                    bool is_ready = msg.value("ready", true);
                    data->is_ready = is_ready;
                    
                    auto& ctx = active_lobbies[data->lobby_code];
                    ctx.player_ready[data->player_id] = is_ready;
                    
                    std::string topic = "lobby:" + data->lobby_code;
                    std::string payload = json{
                        {"type", "player_ready"},
                        {"username", data->username},
                        {"ready", is_ready}
                    }.dump();
                    
                    ws->publish(topic, payload, uWS::OpCode::TEXT, false);
                    ws->send(payload, uWS::OpCode::TEXT);
                    
                    ws->send(payload, uWS::OpCode::TEXT);
                    
                    evaluate_lobby_ready(global_app, data->lobby_code);
                }

                // 6. GET HISTORY
                else if (type == "get_history") {
                    auto recent = db.GetRecentSubmissions(data->player_id, 5);
                    json matches = json::array();
                    for (const auto& sub : recent) {
                        json item;
                        item["prompt"] = sub.prompt;
                        item["score"] = sub.score.has_value() ? sub.score.value() : 0.0f;
                        item["feedback"] = sub.feedback;
                        item["image"] = sub.image_base64;
                        item["date"] = sub.submitted_at;
                        matches.push_back(item);
                    }
                    ws->send(json{{"type", "history_results"}, {"matches", matches}}.dump(), uWS::OpCode::TEXT);
                }

                // 7. PEEKING MECHANIC
                else if (type == "start_peek") {
                    if (data->lobby_code.empty() || !active_lobbies.count(data->lobby_code)) return;
                    std::string target_username = msg.value("target", "");
                    if (target_username.empty() || target_username == data->username) return;
                    
                    auto& ctx = active_lobbies[data->lobby_code];
                    ctx.peekers_of[target_username].insert(data->username);
                    
                    if (ctx.peekers_of[target_username].size() == 1) {
                         std::string target_player_id = getPlayerIdByUsernameInLobby(db, data->lobby_code, target_username);
                         if (!target_player_id.empty()) {
                             std::string target_topic = "user:" + target_player_id;
                             global_app->publish(target_topic, json{{"type", "peek_alert"}, {"peeker", data->username}}.dump(), uWS::OpCode::TEXT, false);
                         }
                    }
                }
                else if (type == "stop_peek") {
                    if (data->lobby_code.empty() || !active_lobbies.count(data->lobby_code)) return;
                    std::string target_username = msg.value("target", "");
                    
                    auto& ctx = active_lobbies[data->lobby_code];
                    ctx.peekers_of[target_username].erase(data->username);
                    
                    if (ctx.peekers_of[target_username].empty()) {
                         std::string target_player_id = getPlayerIdByUsernameInLobby(db, data->lobby_code, target_username);
                         if (!target_player_id.empty()) {
                             std::string target_topic = "user:" + target_player_id;
                             global_app->publish(target_topic, json{{"type", "stop_peek_alert"}}.dump(), uWS::OpCode::TEXT, false);
                         }
                    }
                }
                else if (type == "peek_stream") {
                    if (data->lobby_code.empty() || !active_lobbies.count(data->lobby_code)) return;
                    std::string b64_image = msg.value("image", "");
                    
                    auto& ctx = active_lobbies[data->lobby_code];
                    auto& peekers = ctx.peekers_of[data->username];
                    
                    if (!peekers.empty()) {
                        std::string payload = json{{"type", "peek_stream"}, {"image", b64_image}}.dump();
                        for (const auto& peeker_username : peekers) {
                             std::string peeker_player_id = getPlayerIdByUsernameInLobby(db, data->lobby_code, peeker_username);
                             if (!peeker_player_id.empty()) {
                                 std::string peeker_topic = "user:" + peeker_player_id;
                                 global_app->publish(peeker_topic, payload, uWS::OpCode::TEXT, false);
                             }
                        }
                    }
                }
                
                // 8. SABOTAGE MECHANIC
                else if (type == "sabotage") {
                    if (data->lobby_code.empty() || !active_lobbies.count(data->lobby_code)) return;
                    std::string target_username = msg.value("target", "");
                    std::string attack_type = msg.value("attack", "");
                    
                    std::string target_player_id = getPlayerIdByUsernameInLobby(db, data->lobby_code, target_username);
                    if (!target_player_id.empty()) {
                        std::string target_topic = "user:" + target_player_id;
                        global_app->publish(target_topic, json{
                            {"type", "sabotage_alert"}, 
                            {"attack", attack_type},
                            {"attacker", data->username}
                        }.dump(), uWS::OpCode::TEXT, false);
                        
                        // Notify lobby for event log
                        std::string lobby_topic = "lobby:" + data->lobby_code;
                        global_app->publish(lobby_topic, json{
                            {"type", "game_event"},
                            {"message", data->username + " used " + attack_type + " on " + target_username + "!"},
                            {"bad", true}
                        }.dump(), uWS::OpCode::TEXT, false);
                    }
                }

            } catch (const json::parse_error& e) {
                ws->send(json{{"type", "error"}, {"message", "Invalid JSON"}}.dump(), uWS::OpCode::TEXT);
            }
        },

        .close = [&db, &active_lobbies, trigger_scoring, global_app = &app, &active_players, evaluate_lobby_ready](auto* ws, int code, std::string_view message) {
            auto* data = ws->getUserData();
            if (data->is_authenticated) {
                spdlog::info("Player disconnected, deleting account: {}", data->username);
                if (!data->lobby_code.empty()) {
                    std::string lobby_code = data->lobby_code;
                    
                    // Remove from lobby context
                    if (active_lobbies.count(lobby_code)) {
                        auto& ctx = active_lobbies[lobby_code];
                        ctx.player_ready.erase(data->player_id);
                        
                        // Check if we need to trigger scoring (last player holding up the round)
                        if (ctx.round_active && ctx.submissions.size() >= ctx.player_ready.size() && !ctx.player_ready.empty()) {
                            trigger_scoring(global_app, lobby_code);
                        }
                    }
                    
                    // Notify lobby they left
                    std::string topic = "lobby:" + lobby_code;
                    ws->publish(topic, json{
                        {"type", "player_left"},
                        {"username", data->username}
                    }.dump(), uWS::OpCode::TEXT, false);
                }
                active_players.erase(data->player_id);
                // Ephemeral accounts: delete user immediately
                db.DeleteUser(data->player_id);
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
