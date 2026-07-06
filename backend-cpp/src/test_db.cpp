/**
 * @file test_db.cpp
 * @brief Database smoke test — verifies SQLite + DatabaseManager work.
 *
 * Build:  cmake --build build
 * Run:    ./build/test_db
 *
 * Creates a temporary "test_drawfusion.db", runs all operations, prints results.
 */

#include <iostream>
#include <cstdio>  // for std::remove (delete file)

#include <spdlog/spdlog.h>
#include <spdlog/sinks/stdout_color_sinks.h>

#include "db_manager.h"

int main() {
    auto console = spdlog::stdout_color_mt("test_db");
    spdlog::set_default_logger(console);
    spdlog::set_pattern("%H:%M:%S | %^%l%$ | %v");

    spdlog::info("=== DrawFusion Database Smoke Test ===");
    spdlog::info("");

    const std::string db_file = "test_drawfusion.db";
    int passed = 0, failed = 0;

    // ── Open database (creates file + tables) ───────────────────
    drawfusion::DatabaseManager db(db_file);

    if (!db.IsOpen()) {
        spdlog::error("FAILED: Could not open database");
        return 1;
    }
    spdlog::info("  [PASS] Database opened");
    ++passed;

    // ── Test 1: Create a user ───────────────────────────────────
    auto user_id = db.CreateUser("monu", "monu@example.com", "hashed_password_123");
    if (user_id) {
        spdlog::info("  [PASS] Created user: id={}", *user_id);
        ++passed;
    } else {
        spdlog::error("  [FAIL] CreateUser returned nullopt");
        ++failed;
    }

    // ── Test 2: Check user exists ───────────────────────────────
    if (db.UserExists("monu@example.com")) {
        spdlog::info("  [PASS] UserExists found the user");
        ++passed;
    } else {
        spdlog::error("  [FAIL] UserExists returned false");
        ++failed;
    }

    // ── Test 3: Get user by email ───────────────────────────────
    auto user = db.GetUserByEmail("monu@example.com");
    if (user && user->username == "monu" && user->id == *user_id) {
        spdlog::info("  [PASS] GetUserByEmail: username='{}', email='{}'",
            user->username, user->email);
        ++passed;
    } else {
        spdlog::error("  [FAIL] GetUserByEmail mismatch");
        ++failed;
    }

    // ── Test 4: Get user by ID ──────────────────────────────────
    auto user2 = db.GetUserById(*user_id);
    if (user2 && user2->email == "monu@example.com") {
        spdlog::info("  [PASS] GetUserById works");
        ++passed;
    } else {
        spdlog::error("  [FAIL] GetUserById failed");
        ++failed;
    }

    // ── Test 5: Duplicate user should fail ──────────────────────
    auto dup = db.CreateUser("monu", "monu@example.com", "another_hash");
    if (!dup) {
        spdlog::info("  [PASS] Duplicate user correctly rejected");
        ++passed;
    } else {
        spdlog::error("  [FAIL] Duplicate user was allowed!");
        ++failed;
    }

    // ── Test 6: Create a lobby ──────────────────────────────────
    auto lobby_id = db.CreateLobby(*user_id, "A7X9K2", 4, "2099-12-31T23:59:59");
    if (lobby_id) {
        spdlog::info("  [PASS] Created lobby: code=A7X9K2, id={}", *lobby_id);
        ++passed;
    } else {
        spdlog::error("  [FAIL] CreateLobby failed");
        ++failed;
    }

    // ── Test 7: Lookup lobby by code ────────────────────────────
    auto lobby = db.GetLobbyByCode("A7X9K2");
    if (lobby && lobby->host_player_id == *user_id && lobby->status == "waiting") {
        spdlog::info("  [PASS] GetLobbyByCode: status='{}', max_players={}",
            lobby->status, lobby->max_players);
        ++passed;
    } else {
        spdlog::error("  [FAIL] GetLobbyByCode mismatch");
        ++failed;
    }

    // ── Test 8: Add player to lobby ─────────────────────────────
    if (db.AddPlayerToLobby(*lobby_id, *user_id, false)) {
        spdlog::info("  [PASS] Added player to lobby");
        ++passed;
    } else {
        spdlog::error("  [FAIL] AddPlayerToLobby failed");
        ++failed;
    }

    // ── Test 9: Get lobby players ───────────────────────────────
    auto players = db.GetLobbyPlayers(*lobby_id);
    if (players.size() == 1 && players[0].player_id == *user_id) {
        spdlog::info("  [PASS] GetLobbyPlayers: {} player(s)", players.size());
        ++passed;
    } else {
        spdlog::error("  [FAIL] GetLobbyPlayers wrong count: {}", players.size());
        ++failed;
    }

    // ── Test 10: Create game session ────────────────────────────
    auto session_id = db.CreateGameSession(*lobby_id, "singleplayer", 3);
    if (session_id) {
        spdlog::info("  [PASS] Created game session: {}", *session_id);
        ++passed;
    } else {
        spdlog::error("  [FAIL] CreateGameSession failed");
        ++failed;
    }

    // ── Test 11: Create a round ─────────────────────────────────
    auto round_id = db.CreateRound(*session_id, 1, "Draw a cat on a skateboard", "animal", 60);
    if (round_id) {
        spdlog::info("  [PASS] Created round 1: {}", *round_id);
        ++passed;
    } else {
        spdlog::error("  [FAIL] CreateRound failed");
        ++failed;
    }

    // ── Test 12: Save a submission ──────────────────────────────
    auto sub_id = db.SaveSubmission(*round_id, *user_id, "iVBORw0KGgo=");  // fake base64
    if (sub_id) {
        spdlog::info("  [PASS] Saved submission: {}", *sub_id);
        ++passed;
    } else {
        spdlog::error("  [FAIL] SaveSubmission failed");
        ++failed;
    }

    // ── Test 13: Update submission score ────────────────────────
    if (db.UpdateSubmissionScore(*sub_id, 0.87f, "Great cat!", 0.92f)) {
        spdlog::info("  [PASS] Updated submission score to 0.87");
        ++passed;
    } else {
        spdlog::error("  [FAIL] UpdateSubmissionScore failed");
        ++failed;
    }

    // ── Test 14: Get round submissions ──────────────────────────
    auto subs = db.GetRoundSubmissions(*round_id);
    if (subs.size() == 1 && subs[0].score.has_value() && subs[0].score.value() > 0.86f
        && subs[0].feedback == "Great cat!") {
        spdlog::info("  [PASS] GetRoundSubmissions: score={:.2f}, feedback='{}'",
            subs[0].score.value(), subs[0].feedback);
        ++passed;
    } else {
        spdlog::error("  [FAIL] GetRoundSubmissions data mismatch");
        ++failed;
    }

    // ── Summary ─────────────────────────────────────────────────
    spdlog::info("");
    spdlog::info("────────────────────────────────────────");
    spdlog::info("  RESULTS: {}/{} passed", passed, passed + failed);
    spdlog::info("────────────────────────────────────────");

    if (failed == 0) {
        spdlog::info("  All database operations verified!");
    } else {
        spdlog::error("  {} test(s) FAILED", failed);
    }

    // Clean up: delete the test database file
    std::remove(db_file.c_str());
    spdlog::info("  Cleaned up: deleted {}", db_file);

    return failed > 0 ? 1 : 0;
}
