/**
 * @file test_libs.cpp
 * @brief Library smoke test — verifies all 8 dependencies compile and link.
 *
 * Run this binary to confirm every library is working before building
 * the actual game server. No external services needed.
 *
 * Usage:
 *     ./test_libs
 */

#include <iostream>
#include <string>
#include <vector>

// ── 1. spdlog + fmt ─────────────────────────────────────────────
#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>
#include <fmt/format.h>

// ── 2. nlohmann-json ────────────────────────────────────────────
#include <nlohmann/json.hpp>

// ── 3. jwt-cpp ──────────────────────────────────────────────────
#include <jwt-cpp/jwt.h>
#include <jwt-cpp/traits/nlohmann-json/defaults.h>

// ── 4. gRPC + Protobuf ─────────────────────────────────────────
#include <grpcpp/grpcpp.h>
#include "game-ai.pb.h"
#include "game-ai.grpc.pb.h"

// ── 5. libpqxx ──────────────────────────────────────────────────
#include <pqxx/pqxx>

// ── 6. uWebSockets (header check) ──────────────────────────────
#include <uwebsockets/App.h>

// ── AI Client ───────────────────────────────────────────────────
#include "ai_client.h"

using json = nlohmann::json;

// Test result tracking
struct TestResult {
    std::string name;
    bool passed;
    std::string detail;
};

std::vector<TestResult> results;

void run_test(const std::string& name, auto fn) {
    try {
        auto detail = fn();
        results.push_back({name, true, detail});
        spdlog::info("  ✅ {} — {}", name, detail);
    } catch (const std::exception& e) {
        results.push_back({name, false, e.what()});
        spdlog::error("  ❌ {} — {}", name, e.what());
    }
}

int main() {
    // ── Setup logger ────────────────────────────────────────────
    auto console = spdlog::stdout_color_mt("test");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("%H:%M:%S │ %^%l%$ │ %v");

    spdlog::info("╔══════════════════════════════════════════════════╗");
    spdlog::info("║  DrawFusion — C++ Library Smoke Test             ║");
    spdlog::info("╚══════════════════════════════════════════════════╝");
    spdlog::info("");

    // ── Test 1: spdlog + fmt ────────────────────────────────────
    run_test("spdlog + fmt", []() -> std::string {
        std::string msg = fmt::format("fmt works: {} + {} = {}", 2, 3, 5);
        spdlog::debug("spdlog debug logging works");
        return msg;
    });

    // ── Test 2: nlohmann-json ───────────────────────────────────
    run_test("nlohmann-json", []() -> std::string {
        json j = {
            {"player_id", "player-001"},
            {"action", "draw"},
            {"score", 0.85},
            {"tags", {"cat", "animal", "cute"}}
        };
        std::string serialized = j.dump();
        json parsed = json::parse(serialized);
        return fmt::format("Serialized {} bytes, parsed {} keys",
            serialized.size(), parsed.size());
    });

    // ── Test 3: jwt-cpp ─────────────────────────────────────────
    run_test("jwt-cpp", []() -> std::string {
        // Create a JWT token
        auto token = jwt::create()
            .set_issuer("drawfusion")
            .set_subject("player-001")
            .set_payload_claim("username", jwt::claim(std::string("testuser")))
            .set_issued_at(std::chrono::system_clock::now())
            .set_expires_at(std::chrono::system_clock::now() + std::chrono::hours(24))
            .sign(jwt::algorithm::hs256{"drawfusion-secret-key"});

        // Verify the token
        auto verifier = jwt::verify()
            .allow_algorithm(jwt::algorithm::hs256{"drawfusion-secret-key"})
            .with_issuer("drawfusion");

        auto decoded = jwt::decode(token);
        verifier.verify(decoded);

        return fmt::format("Token created ({} chars), verified OK", token.size());
    });

    // ── Test 4: Protobuf message ────────────────────────────────
    run_test("protobuf", []() -> std::string {
        drawfusion::PromptRequest req;
        req.set_game_id("test-game-001");
        req.set_difficulty("medium");
        req.add_past_prompts("A cat");
        req.add_past_prompts("A dog");

        std::string serialized;
        req.SerializeToString(&serialized);

        drawfusion::PromptRequest parsed;
        parsed.ParseFromString(serialized);

        return fmt::format("Serialized {} bytes: game_id='{}', difficulty='{}', past_prompts={}",
            serialized.size(), parsed.game_id(), parsed.difficulty(),
            parsed.past_prompts_size());
    });

    // ── Test 5: gRPC channel creation ───────────────────────────
    run_test("gRPC channel", []() -> std::string {
        // Create a channel (doesn't connect until first RPC)
        auto channel = grpc::CreateChannel(
            "localhost:50051",
            grpc::InsecureChannelCredentials()
        );
        auto state = channel->GetState(false);  // false = don't try to connect

        // Create a stub (proves generated gRPC code links correctly)
        auto stub = drawfusion::GameMaster::NewStub(channel);

        return fmt::format("Channel created (state={}), stub ready",
            static_cast<int>(state));
    });

    // ── Test 6: AI Client wrapper ───────────────────────────────
    run_test("AI Client (wrapper)", []() -> std::string {
        drawfusion::AIClientConfig config;
        config.target_address = "localhost:50051";
        config.deadline_ms = 2000;
        config.max_retries = 1;

        drawfusion::GameMasterClient client(config);
        // Don't call IsHealthy() — server might not be running
        return "GameMasterClient constructed OK";
    });

    // ── Test 7: libpqxx (compile check) ─────────────────────────
    run_test("libpqxx (compile)", []() -> std::string {
        // Just verify we can create a connection string parser
        // Don't actually connect — PostgreSQL might not be running
        try {
            pqxx::connection c{"postgresql://invalid:5432/test"};
        } catch (const pqxx::broken_connection&) {
            // Expected — we're not actually connecting
        }
        return "libpqxx headers + linking OK (no DB connection attempted)";
    });

    // ── Test 8: uWebSockets (compile check) ─────────────────────
    run_test("uWebSockets (compile)", []() -> std::string {
        // Verify uWebSockets types are available
        // Don't start a server — just check it compiles
        // uWS::App is a template; instantiation proves headers work
        (void)sizeof(uWS::App);
        return "uWebSockets headers OK";
    });

    // ── Summary ─────────────────────────────────────────────────
    spdlog::info("");
    spdlog::info("────────────────────────────────────────────────");
    spdlog::info("  SUMMARY");
    spdlog::info("────────────────────────────────────────────────");

    int passed = 0, failed = 0;
    for (const auto& r : results) {
        if (r.passed) ++passed;
        else ++failed;
    }

    spdlog::info("  {}/{} tests passed", passed, results.size());

    if (failed == 0) {
        spdlog::info("");
        spdlog::info("  🎉 All libraries verified! Backend is ready to build.");
        spdlog::info("  → Next: start the Python gRPC server, then run drawfusion_server");
    } else {
        spdlog::error("");
        spdlog::error("  ⚠️  {} test(s) failed. Fix before proceeding.", failed);
    }

    return failed > 0 ? 1 : 0;
}
