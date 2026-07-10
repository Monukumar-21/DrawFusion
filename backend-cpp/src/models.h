/**
 * @file models.h
 * @brief Data model structs — C++ representations of database rows.
 *
 * Each struct maps 1:1 to a SQLite table defined in the schema.
 * These are plain data holders; the DatabaseManager handles all
 * persistence logic.
 */

#pragma once

#include <optional>
#include <string>
#include <vector>

namespace drawfusion {

// ── User ────────────────────────────────────────────────────────────
// Maps to: `users` table

struct User {
    std::string id;
    std::string username;
    std::string email;
    std::string password_hash;
    std::string created_at;      // ISO 8601
};

// ── Lobby ───────────────────────────────────────────────────────────
// Maps to: `lobbies` table
// Lifecycle: waiting → started → expired

struct Lobby {
    std::string id;
    std::string code;            // 6-char join code (e.g. "A7X9K2")
    std::string host_player_id;
    int max_players = 4;
    std::string status;          // "waiting" | "started" | "expired"
    std::string created_at;
    std::string expires_at;
};

// ── LobbyPlayer ─────────────────────────────────────────────────────
// Maps to: `lobby_players` junction table (many-to-many: users ↔ lobbies)

struct LobbyPlayer {
    std::string id;
    std::string lobby_id;
    std::string player_id;
    bool is_ai_bot = false;
    std::string joined_at;
};

// ── GameSession ─────────────────────────────────────────────────────
// Maps to: `game_sessions` table
// Created when a lobby starts; lifecycle: active → completed

struct GameSession {
    std::string id;
    std::string lobby_id;
    std::string mode;            // "singleplayer" | "multiplayer"
    std::string status;          // "active" | "completed"
    int total_rounds = 3;
    std::string created_at;
    std::string ended_at;        // Empty until game finishes
};

// ── Round ───────────────────────────────────────────────────────────
// Maps to: `rounds` table

struct Round {
    std::string id;
    std::string session_id;
    int round_number = 0;
    std::string prompt;          // AI-generated drawing prompt
    std::string category;        // "animal", "vehicle", etc.
    int duration_seconds = 60;
    std::string started_at;
    std::string ended_at;
};

// ── Submission ──────────────────────────────────────────────────────
// Maps to: `submissions` table
// A player's drawing + AI score for one round

struct Submission {
    std::string id;
    std::string round_id;
    std::string player_id;
    std::string image_base64;          // PNG canvas export
    std::optional<float> score;        // 0.0–1.0, nullopt until judged
    std::string feedback;
    std::optional<float> confidence;   // AI confidence, nullopt until judged
    std::string submitted_at;
};

// ── SubmissionHistory ───────────────────────────────────────────────
// Maps to: joined query of submissions + rounds
// Used for the user's match history view

struct SubmissionHistory {
    std::string prompt;
    std::optional<float> score;
    std::string feedback;
    std::string image_base64;
    std::string submitted_at;
};

} // namespace drawfusion
