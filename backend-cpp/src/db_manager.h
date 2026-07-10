/**
 * @file db_manager.h
 * @brief DatabaseManager — SQLite wrapper for all database operations.
 *
 * Provides a typed C++ interface over raw SQLite3, so the rest of the
 * server never touches SQL directly.
 *
 * Usage:
 *     DatabaseManager db("drawfusion.db");
 *     auto user_id = db.CreateUser("monu", "monu@email.com", "hashed_pw");
 *     auto user = db.GetUserByEmail("monu@email.com");
 */

#pragma once

#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <sqlite3.h>

#include "models.h"

namespace drawfusion {

/**
 * Wraps SQLite3 and provides typed operations for every table.
 *
 * Thread safety: SQLite in WAL mode + serialized threading mode.
 * Safe for concurrent reads, serialized writes — perfect for our use case.
 */
class DatabaseManager {
public:
    /**
     * Open (or create) the database file and run schema migration.
     * @param db_path Path to the .db file (e.g. "drawfusion.db")
     */
    explicit DatabaseManager(const std::string& db_path);

    /**
     * Close the database connection cleanly.
     */
    ~DatabaseManager();

    // Non-copyable (only one connection owner)
    DatabaseManager(const DatabaseManager&) = delete;
    DatabaseManager& operator=(const DatabaseManager&) = delete;

    // Movable
    DatabaseManager(DatabaseManager&& other) noexcept;
    DatabaseManager& operator=(DatabaseManager&& other) noexcept;

    /**
     * Check if the database is open and functional.
     */
    bool IsOpen() const;

    /**
     * Reclaims unused disk space by running the SQLite VACUUM command.
     */
    void Vacuum();

    // ══════════════════════════════════════════════════════════════
    //  USER OPERATIONS
    // ══════════════════════════════════════════════════════════════

    /**
     * Register a new user.
     * @return The generated UUID for the new user, or nullopt on failure
     */
    std::optional<std::string> CreateUser(
        const std::string& username,
        const std::string& email,
        const std::string& password_hash
    );

    /**
     * Find a user by their email address (for login).
     */
    std::optional<User> GetUserByEmail(const std::string& email);

    /**
     * Find a user by their ID.
     */
    std::optional<User> GetUserById(const std::string& id);

    /**
     * Delete a user and cascade delete all their data.
     */
    bool DeleteUser(const std::string& id);

    /**
     * Update a user's password.
     */
    bool UpdatePassword(const std::string& id, const std::string& new_password_hash);

    /**
     * Check if a user with this email already exists.
     */
    bool UserExists(const std::string& email);

    /**
     * Get the total number of registered users.
     */
    int GetUserCount();

    // ══════════════════════════════════════════════════════════════
    //  LOBBY OPERATIONS
    // ══════════════════════════════════════════════════════════════

    /**
     * Create a new lobby with a unique join code.
     * @return The generated UUID for the new lobby, or nullopt on failure
     */
    std::optional<std::string> CreateLobby(
        const std::string& host_player_id,
        const std::string& code,
        int max_players,
        const std::string& expires_at
    );

    /**
     * Look up a lobby by its join code.
     */
    std::optional<Lobby> GetLobbyByCode(const std::string& code);

    /**
     * Add a player (human or AI bot) to a lobby.
     */
    bool AddPlayerToLobby(
        const std::string& lobby_id,
        const std::string& player_id,
        bool is_ai_bot = false
    );

    /**
     * Get all players in a lobby.
     */
    std::vector<LobbyPlayer> GetLobbyPlayers(const std::string& lobby_id);

    /**
     * Remove a player from a lobby.
     */
    bool RemovePlayerFromLobby(const std::string& lobby_id, const std::string& player_id);

    /**
     * Delete a lobby entirely.
     */
    bool DeleteLobby(const std::string& lobby_id);

    /**
     * Update a lobby's status (waiting → started, waiting → expired).
     */
    bool UpdateLobbyStatus(const std::string& lobby_id, const std::string& status);

    /**
     * Mark all lobbies whose expires_at has passed as "expired".
     * @return Number of lobbies expired
     */
    int ExpireStaleLobbies();

    /**
     * Get the total number of currently active/waiting lobbies.
     */
    int GetActiveLobbyCount();

    // ══════════════════════════════════════════════════════════════
    //  GAME SESSION OPERATIONS
    // ══════════════════════════════════════════════════════════════

    /**
     * Create a new game session when a lobby starts.
     * @return The generated UUID for the game session
     */
    std::optional<std::string> CreateGameSession(
        const std::string& lobby_id,
        const std::string& mode,
        int total_rounds = 3
    );

    /**
     * Create a new round within a game session.
     * @return The generated UUID for the round
     */
    std::optional<std::string> CreateRound(
        const std::string& session_id,
        int round_number,
        const std::string& prompt,
        const std::string& category,
        int duration_seconds = 60
    );

    /**
     * Update game session status (active → completed).
     */
    bool UpdateGameStatus(const std::string& session_id, const std::string& status);

    // ══════════════════════════════════════════════════════════════
    //  SUBMISSION OPERATIONS
    // ══════════════════════════════════════════════════════════════

    /**
     * Save a player's drawing submission for a round.
     * @return The generated UUID for the submission
     */
    std::optional<std::string> SaveSubmission(
        const std::string& round_id,
        const std::string& player_id,
        const std::string& image_base64
    );

    /**
     * Update a submission's score after AI judging.
     */
    bool UpdateSubmissionScore(
        const std::string& submission_id,
        float score,
        const std::string& feedback,
        float confidence
    );

    /**
     * Get all submissions for a round (for ranking/results).
     */
    std::vector<Submission> GetRoundSubmissions(const std::string& round_id);

    /**
     * Get the user's past drawing submissions and scores.
     */
    std::vector<SubmissionHistory> GetRecentSubmissions(const std::string& player_id, int limit = 5);

private:
    sqlite3* db_ = nullptr;      // Raw SQLite3 connection handle
    std::string db_path_;

    /**
     * Read the schema.sql file and execute it to create tables.
     */
    void RunMigrations();

    /**
     * Generate a UUID v4 string (random-based).
     * No external library needed — uses <random>.
     */
    static std::string GenerateUUID();

    /**
     * Execute a SQL statement that doesn't return rows (INSERT, UPDATE, DELETE).
     * @return true on success
     */
    bool Execute(const std::string& sql);

    /**
     * Log a SQLite error with context.
     */
    void LogError(const std::string& context);
};

} // namespace drawfusion
