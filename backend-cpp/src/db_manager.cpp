/**
 * @file db_manager.cpp
 * @brief Implementation of DatabaseManager — SQLite operations for DrawFusion.
 *
 * Uses the SQLite3 C API with parameterized queries throughout
 * to prevent SQL injection. All statements use the prepare → bind → step
 * pattern wrapped by StmtGuard for automatic cleanup.
 */

#include "db_manager.h"

#include <random>
#include <sstream>
#include <iomanip>
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace drawfusion {

// ═══════════════════════════════════════════════════════════════════
//  RAII Helper — auto-finalizes SQLite prepared statements
// ═══════════════════════════════════════════════════════════════════

class StmtGuard {
public:
    StmtGuard(sqlite3* db, const std::string& sql) : db_(db) {
        int rc = sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt_, nullptr);
        if (rc != SQLITE_OK) {
            spdlog::error("[DB] Failed to prepare: {} — {}", sql, sqlite3_errmsg(db_));
            stmt_ = nullptr;
        }
    }

    ~StmtGuard() {
        if (stmt_) sqlite3_finalize(stmt_);
    }

    // No copying
    StmtGuard(const StmtGuard&) = delete;
    StmtGuard& operator=(const StmtGuard&) = delete;

    sqlite3_stmt* get() { return stmt_; }
    bool ok() const { return stmt_ != nullptr; }

    // Convenience: bind parameters by index (1-based, as SQLite uses)
    void bind_text(int idx, const std::string& val) {
        sqlite3_bind_text(stmt_, idx, val.c_str(), val.size(), SQLITE_TRANSIENT);
    }
    void bind_int(int idx, int val) {
        sqlite3_bind_int(stmt_, idx, val);
    }
    void bind_double(int idx, double val) {
        sqlite3_bind_double(stmt_, idx, val);
    }

    // Read column values from current row (0-based index)
    std::string col_text(int idx) {
        const char* val = reinterpret_cast<const char*>(sqlite3_column_text(stmt_, idx));
        return val ? val : "";
    }
    int col_int(int idx) {
        return sqlite3_column_int(stmt_, idx);
    }
    double col_double(int idx) {
        return sqlite3_column_double(stmt_, idx);
    }

    bool step() {
        return sqlite3_step(stmt_) == SQLITE_ROW;
    }
    bool exec() {
        int rc = sqlite3_step(stmt_);
        return rc == SQLITE_DONE;
    }

private:
    sqlite3* db_;
    sqlite3_stmt* stmt_ = nullptr;
};


// ═══════════════════════════════════════════════════════════════════
//  Constructor / Destructor
// ═══════════════════════════════════════════════════════════════════

DatabaseManager::DatabaseManager(const std::string& db_path)
    : db_path_(db_path)
{
    // Open (or create) the database file
    int rc = sqlite3_open(db_path.c_str(), &db_);
    if (rc != SQLITE_OK) {
        spdlog::error("[DB] Failed to open database '{}': {}", db_path, sqlite3_errmsg(db_));
        db_ = nullptr;
        return;
    }

    spdlog::info("[DB] Opened database: {}", db_path);

    // WAL mode = faster concurrent reads (like Python's threading mode)
    Execute("PRAGMA journal_mode=WAL");
    // Enable foreign keys (SQLite has them OFF by default!)
    Execute("PRAGMA foreign_keys=ON");
    // Enable incremental vacuuming to prevent disk bloat
    Execute("PRAGMA auto_vacuum=INCREMENTAL");
    // Busy timeout: wait 5 seconds if another thread has the lock
    sqlite3_busy_timeout(db_, 5000);

    RunMigrations();
}

DatabaseManager::~DatabaseManager() {
    if (db_) {
        sqlite3_close(db_);
        spdlog::info("[DB] Database closed: {}", db_path_);
    }
}

// Move constructor
DatabaseManager::DatabaseManager(DatabaseManager&& other) noexcept
    : db_(other.db_), db_path_(std::move(other.db_path_))
{
    other.db_ = nullptr;
}

// Move assignment
DatabaseManager& DatabaseManager::operator=(DatabaseManager&& other) noexcept {
    if (this != &other) {
        if (db_) sqlite3_close(db_);
        db_ = other.db_;
        db_path_ = std::move(other.db_path_);
        other.db_ = nullptr;
    }
    return *this;
}

bool DatabaseManager::IsOpen() const {
    return db_ != nullptr;
}


// ═══════════════════════════════════════════════════════════════════
//  Schema Migration — idempotent via CREATE TABLE IF NOT EXISTS
// ═══════════════════════════════════════════════════════════════════

void DatabaseManager::RunMigrations() {
    spdlog::info("[DB] Running schema migrations...");

    // ⚠️ This inline SQL is the RUNTIME schema (single source of truth).
    // If you change this, also update database/schema.sql for documentation.

    const char* schema = R"SQL(
        PRAGMA foreign_keys = ON;

        CREATE TABLE IF NOT EXISTS users (
            id            TEXT PRIMARY KEY,
            username      TEXT NOT NULL UNIQUE,
            email         TEXT NOT NULL UNIQUE,
            password_hash TEXT NOT NULL,
            created_at    TEXT NOT NULL DEFAULT (datetime('now'))
        );
        CREATE INDEX IF NOT EXISTS idx_users_email ON users(email);

        CREATE TABLE IF NOT EXISTS lobbies (
            id              TEXT PRIMARY KEY,
            code            TEXT NOT NULL UNIQUE,
            host_player_id  TEXT NOT NULL,
            max_players     INTEGER NOT NULL DEFAULT 4,
            status          TEXT NOT NULL DEFAULT 'waiting',
            created_at      TEXT NOT NULL DEFAULT (datetime('now')),
            expires_at      TEXT NOT NULL,
            FOREIGN KEY (host_player_id) REFERENCES users(id) ON DELETE CASCADE
        );
        CREATE INDEX IF NOT EXISTS idx_lobbies_code ON lobbies(code);
        CREATE INDEX IF NOT EXISTS idx_lobbies_status ON lobbies(status);

        CREATE TABLE IF NOT EXISTS lobby_players (
            id          TEXT PRIMARY KEY,
            lobby_id    TEXT NOT NULL,
            player_id   TEXT NOT NULL,
            is_ai_bot   INTEGER NOT NULL DEFAULT 0,
            joined_at   TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (lobby_id)  REFERENCES lobbies(id) ON DELETE CASCADE,
            FOREIGN KEY (player_id) REFERENCES users(id)   ON DELETE CASCADE,
            UNIQUE(lobby_id, player_id)
        );

        CREATE TABLE IF NOT EXISTS game_sessions (
            id           TEXT PRIMARY KEY,
            lobby_id     TEXT NOT NULL,
            mode         TEXT NOT NULL DEFAULT 'multiplayer',
            status       TEXT NOT NULL DEFAULT 'active',
            total_rounds INTEGER NOT NULL DEFAULT 3,
            created_at   TEXT NOT NULL DEFAULT (datetime('now')),
            ended_at     TEXT,
            FOREIGN KEY (lobby_id) REFERENCES lobbies(id) ON DELETE CASCADE
        );

        CREATE TABLE IF NOT EXISTS rounds (
            id               TEXT PRIMARY KEY,
            session_id       TEXT NOT NULL,
            round_number     INTEGER NOT NULL,
            prompt           TEXT NOT NULL,
            category         TEXT,
            duration_seconds INTEGER NOT NULL DEFAULT 60,
            started_at       TEXT NOT NULL DEFAULT (datetime('now')),
            ended_at         TEXT,
            FOREIGN KEY (session_id) REFERENCES game_sessions(id) ON DELETE CASCADE,
            UNIQUE(session_id, round_number)
        );

        CREATE TABLE IF NOT EXISTS submissions (
            id           TEXT PRIMARY KEY,
            round_id     TEXT NOT NULL,
            player_id    TEXT NOT NULL,
            image_base64 TEXT,
            score        REAL,
            feedback     TEXT,
            confidence   REAL,
            submitted_at TEXT NOT NULL DEFAULT (datetime('now')),
            FOREIGN KEY (round_id)  REFERENCES rounds(id) ON DELETE CASCADE,
            FOREIGN KEY (player_id) REFERENCES users(id)  ON DELETE CASCADE,
            UNIQUE(round_id, player_id)
        );
        CREATE INDEX IF NOT EXISTS idx_submissions_round ON submissions(round_id);
    )SQL";

    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        spdlog::error("[DB] Migration failed: {}", err_msg ? err_msg : "unknown error");
        sqlite3_free(err_msg);
    } else {
        spdlog::info("[DB] ✅ Schema migration complete — 6 tables ready");
    }
}


// ═══════════════════════════════════════════════════════════════════
//  UUID v4 Generator — uses <random>, no external library needed
// ═══════════════════════════════════════════════════════════════════

std::string DatabaseManager::GenerateUUID() {
    static thread_local std::mt19937 gen(std::random_device{}());
    std::uniform_int_distribution<int> dist(0, 15);
    std::uniform_int_distribution<int> dist2(8, 11);  // For the "y" position

    const char* hex = "0123456789abcdef";
    std::string uuid = "xxxxxxxx-xxxx-4xxx-yxxx-xxxxxxxxxxxx";

    for (auto& c : uuid) {
        if (c == 'x') c = hex[dist(gen)];
        else if (c == 'y') c = hex[dist2(gen)];
        // '-' and '4' stay as-is
    }

    return uuid;
}


// ═══════════════════════════════════════════════════════════════════
//  Utility Methods
// ═══════════════════════════════════════════════════════════════════

bool DatabaseManager::Execute(const std::string& sql) {
    char* err_msg = nullptr;
    int rc = sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &err_msg);
    if (rc != SQLITE_OK) {
        spdlog::error("[DB] Execute failed: {} — {}", sql, err_msg ? err_msg : "");
        sqlite3_free(err_msg);
        return false;
    }
    return true;
}

void DatabaseManager::LogError(const std::string& context) {
    spdlog::error("[DB] {} — {}", context, sqlite3_errmsg(db_));
}

void DatabaseManager::Vacuum() {
    spdlog::info("[DB] Vacuuming database to reclaim disk space...");
    if (Execute("PRAGMA incremental_vacuum")) {
        spdlog::info("[DB] Vacuum complete.");
    }
}


// ═══════════════════════════════════════════════════════════════════
//  USER OPERATIONS
// ═══════════════════════════════════════════════════════════════════

std::optional<std::string> DatabaseManager::CreateUser(
    const std::string& username,
    const std::string& email,
    const std::string& password_hash
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO users (id, username, email, password_hash) VALUES (?, ?, ?, ?)");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);
    stmt.bind_text(2, username);
    stmt.bind_text(3, email);
    stmt.bind_text(4, password_hash);

    if (stmt.exec()) {
        spdlog::info("[DB] Created user: {} ({})", username, id);
        return id;
    }

    LogError("CreateUser");
    return std::nullopt;
}

std::optional<User> DatabaseManager::GetUserByEmail(const std::string& email) {
    StmtGuard stmt(db_,
        "SELECT id, username, email, password_hash, created_at FROM users WHERE email = ?");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, email);

    if (stmt.step()) {
        User user;
        user.id            = stmt.col_text(0);
        user.username      = stmt.col_text(1);
        user.email         = stmt.col_text(2);
        user.password_hash = stmt.col_text(3);
        user.created_at    = stmt.col_text(4);
        return user;
    }

    return std::nullopt;  // User not found
}

std::optional<User> DatabaseManager::GetUserById(const std::string& id) {
    StmtGuard stmt(db_,
        "SELECT id, username, email, password_hash, created_at FROM users WHERE id = ?");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);

    if (stmt.step()) {
        User user;
        user.id            = stmt.col_text(0);
        user.username      = stmt.col_text(1);
        user.email         = stmt.col_text(2);
        user.password_hash = stmt.col_text(3);
        user.created_at    = stmt.col_text(4);
        return user;
    }

    return std::nullopt;
}

bool DatabaseManager::DeleteUser(const std::string& id) {
    StmtGuard stmt(db_, "DELETE FROM users WHERE id = ?");
    if (!stmt.ok()) return false;

    stmt.bind_text(1, id);

    if (stmt.exec()) {
        spdlog::info("[DB] Deleted user: {}", id);
        return true;
    }

    LogError("DeleteUser");
    return false;
}

bool DatabaseManager::UpdatePassword(const std::string& id, const std::string& new_password_hash) {
    StmtGuard stmt(db_, "UPDATE users SET password_hash = ? WHERE id = ?");
    if (!stmt.ok()) return false;

    stmt.bind_text(1, new_password_hash);
    stmt.bind_text(2, id);

    if (stmt.exec()) {
        spdlog::info("[DB] Updated password for user: {}", id);
        return true;
    }

    LogError("UpdatePassword");
    return false;
}

bool DatabaseManager::UserExists(const std::string& email) {
    StmtGuard stmt(db_, "SELECT 1 FROM users WHERE email = ?");
    if (!stmt.ok()) return false;

    stmt.bind_text(1, email);
    return stmt.step();  // true if a row was found
}

int DatabaseManager::GetUserCount() {
    StmtGuard stmt(db_, "SELECT COUNT(*) FROM users");
    if (!stmt.ok()) return 0;
    
    if (stmt.step()) {
        return stmt.col_int(0);
    }
    return 0;
}


// ═══════════════════════════════════════════════════════════════════
//  LOBBY OPERATIONS
// ═══════════════════════════════════════════════════════════════════

std::optional<std::string> DatabaseManager::CreateLobby(
    const std::string& host_player_id,
    const std::string& code,
    int max_players,
    const std::string& expires_at
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO lobbies (id, code, host_player_id, max_players, expires_at) "
        "VALUES (?, ?, ?, ?, ?)");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);
    stmt.bind_text(2, code);
    stmt.bind_text(3, host_player_id);
    stmt.bind_int(4, max_players);
    stmt.bind_text(5, expires_at);

    if (stmt.exec()) {
        spdlog::info("[DB] Created lobby: code={} host={} max={}", code, host_player_id, max_players);
        return id;
    }

    LogError("CreateLobby");
    return std::nullopt;
}

std::optional<Lobby> DatabaseManager::GetLobbyByCode(const std::string& code) {
    StmtGuard stmt(db_,
        "SELECT id, code, host_player_id, max_players, status, created_at, expires_at "
        "FROM lobbies WHERE code = ?");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, code);

    if (stmt.step()) {
        Lobby lobby;
        lobby.id              = stmt.col_text(0);
        lobby.code            = stmt.col_text(1);
        lobby.host_player_id  = stmt.col_text(2);
        lobby.max_players     = stmt.col_int(3);
        lobby.status          = stmt.col_text(4);
        lobby.created_at      = stmt.col_text(5);
        lobby.expires_at      = stmt.col_text(6);
        return lobby;
    }

    return std::nullopt;
}

bool DatabaseManager::AddPlayerToLobby(
    const std::string& lobby_id,
    const std::string& player_id,
    bool is_ai_bot
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO lobby_players (id, lobby_id, player_id, is_ai_bot) VALUES (?, ?, ?, ?)");
    if (!stmt.ok()) return false;

    stmt.bind_text(1, id);
    stmt.bind_text(2, lobby_id);
    stmt.bind_text(3, player_id);
    stmt.bind_int(4, is_ai_bot ? 1 : 0);

    if (stmt.exec()) {
        spdlog::info("[DB] Player {} joined lobby {} (bot={})", player_id, lobby_id, is_ai_bot);
        return true;
    }

    LogError("AddPlayerToLobby");
    return false;
}

std::vector<LobbyPlayer> DatabaseManager::GetLobbyPlayers(const std::string& lobby_id) {
    std::vector<LobbyPlayer> players;

    StmtGuard stmt(db_,
        "SELECT id, lobby_id, player_id, is_ai_bot, joined_at "
        "FROM lobby_players WHERE lobby_id = ?");
    if (!stmt.ok()) return players;

    stmt.bind_text(1, lobby_id);

    while (stmt.step()) {
        LobbyPlayer lp;
        lp.id        = stmt.col_text(0);
        lp.lobby_id  = stmt.col_text(1);
        lp.player_id = stmt.col_text(2);
        lp.is_ai_bot = (stmt.col_int(3) == 1);
        lp.joined_at = stmt.col_text(4);
        players.push_back(lp);
    }

    return players;
}

bool DatabaseManager::RemovePlayerFromLobby(const std::string& lobby_id, const std::string& player_id) {
    StmtGuard stmt(db_, "DELETE FROM lobby_players WHERE lobby_id = ? AND player_id = ?");
    if (!stmt.ok()) return false;
    stmt.bind_text(1, lobby_id);
    stmt.bind_text(2, player_id);
    if (stmt.exec()) {
        spdlog::info("[DB] Player {} removed from lobby {}", player_id, lobby_id);
        return true;
    }
    return false;
}

bool DatabaseManager::DeleteLobby(const std::string& lobby_id) {
    StmtGuard stmt(db_, "DELETE FROM lobbies WHERE id = ?");
    if (!stmt.ok()) return false;
    stmt.bind_text(1, lobby_id);
    if (stmt.exec()) {
        spdlog::info("[DB] Lobby {} deleted", lobby_id);
        return true;
    }
    return false;
}

bool DatabaseManager::UpdateLobbyStatus(const std::string& lobby_id, const std::string& status) {
    StmtGuard stmt(db_, "UPDATE lobbies SET status = ? WHERE id = ?");
    if (!stmt.ok()) return false;

    stmt.bind_text(1, status);
    stmt.bind_text(2, lobby_id);

    if (stmt.exec()) {
        spdlog::info("[DB] Lobby {} → status={}", lobby_id, status);
        return true;
    }

    LogError("UpdateLobbyStatus");
    return false;
}

int DatabaseManager::ExpireStaleLobbies() {
    // Mark lobbies as expired if their expires_at has passed AND they're still waiting
    StmtGuard stmt(db_,
        "UPDATE lobbies SET status = 'expired' "
        "WHERE status = 'waiting' AND expires_at < datetime('now')");
    if (!stmt.ok()) return 0;

    if (stmt.exec()) {
        int count = sqlite3_changes(db_);
        if (count > 0) {
            spdlog::info("[DB] Expired {} stale lobbies", count);
        }
        return count;
    }

    return 0;
}

int DatabaseManager::GetActiveLobbyCount() {
    StmtGuard stmt(db_, "SELECT COUNT(*) FROM lobbies WHERE status IN ('waiting', 'active')");
    if (!stmt.ok()) return 0;
    
    if (stmt.step()) {
        return stmt.col_int(0);
    }
    return 0;
}


// ═══════════════════════════════════════════════════════════════════
//  GAME SESSION OPERATIONS
// ═══════════════════════════════════════════════════════════════════

std::optional<std::string> DatabaseManager::CreateGameSession(
    const std::string& lobby_id,
    const std::string& mode,
    int total_rounds
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO game_sessions (id, lobby_id, mode, total_rounds) VALUES (?, ?, ?, ?)");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);
    stmt.bind_text(2, lobby_id);
    stmt.bind_text(3, mode);
    stmt.bind_int(4, total_rounds);

    if (stmt.exec()) {
        spdlog::info("[DB] Created game session: mode={} rounds={} ({})", mode, total_rounds, id);
        return id;
    }

    LogError("CreateGameSession");
    return std::nullopt;
}

std::optional<std::string> DatabaseManager::CreateRound(
    const std::string& session_id,
    int round_number,
    const std::string& prompt,
    const std::string& category,
    int duration_seconds
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO rounds (id, session_id, round_number, prompt, category, duration_seconds) "
        "VALUES (?, ?, ?, ?, ?, ?)");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);
    stmt.bind_text(2, session_id);
    stmt.bind_int(3, round_number);
    stmt.bind_text(4, prompt);
    stmt.bind_text(5, category);
    stmt.bind_int(6, duration_seconds);

    if (stmt.exec()) {
        spdlog::info("[DB] Created round {}: '{}' [{}s]", round_number, prompt, duration_seconds);
        return id;
    }

    LogError("CreateRound");
    return std::nullopt;
}

bool DatabaseManager::UpdateGameStatus(const std::string& session_id, const std::string& status) {
    std::string sql = (status == "completed")
        ? "UPDATE game_sessions SET status = ?, ended_at = datetime('now') WHERE id = ?"
        : "UPDATE game_sessions SET status = ? WHERE id = ?";

    StmtGuard stmt(db_, sql);
    if (!stmt.ok()) return false;

    stmt.bind_text(1, status);
    stmt.bind_text(2, session_id);

    if (stmt.exec()) {
        spdlog::info("[DB] Game session {} → status={}", session_id, status);
        return true;
    }

    LogError("UpdateGameStatus");
    return false;
}


// ═══════════════════════════════════════════════════════════════════
//  SUBMISSION OPERATIONS
// ═══════════════════════════════════════════════════════════════════

std::optional<std::string> DatabaseManager::SaveSubmission(
    const std::string& round_id,
    const std::string& player_id,
    const std::string& image_base64
) {
    std::string id = GenerateUUID();

    StmtGuard stmt(db_,
        "INSERT INTO submissions (id, round_id, player_id, image_base64) VALUES (?, ?, ?, ?)");
    if (!stmt.ok()) return std::nullopt;

    stmt.bind_text(1, id);
    stmt.bind_text(2, round_id);
    stmt.bind_text(3, player_id);
    stmt.bind_text(4, image_base64);

    if (stmt.exec()) {
        spdlog::info("[DB] Saved submission: player={} round={}", player_id, round_id);
        return id;
    }

    LogError("SaveSubmission");
    return std::nullopt;
}

bool DatabaseManager::UpdateSubmissionScore(
    const std::string& submission_id,
    float score,
    const std::string& feedback,
    float confidence
) {
    StmtGuard stmt(db_,
        "UPDATE submissions SET score = ?, feedback = ?, confidence = ? WHERE id = ?");
    if (!stmt.ok()) return false;

    stmt.bind_double(1, score);
    stmt.bind_text(2, feedback);
    stmt.bind_double(3, confidence);
    stmt.bind_text(4, submission_id);

    if (stmt.exec()) {
        spdlog::info("[DB] Scored submission {}: score={:.3f}", submission_id, score);
        return true;
    }

    LogError("UpdateSubmissionScore");
    return false;
}

std::vector<Submission> DatabaseManager::GetRoundSubmissions(const std::string& round_id) {
    std::vector<Submission> submissions;

    StmtGuard stmt(db_,
        "SELECT id, round_id, player_id, image_base64, score, feedback, confidence, submitted_at "
        "FROM submissions WHERE round_id = ? ORDER BY score DESC");
    if (!stmt.ok()) return submissions;

    stmt.bind_text(1, round_id);

    while (stmt.step()) {
        Submission s;
        s.id           = stmt.col_text(0);
        s.round_id     = stmt.col_text(1);
        s.player_id    = stmt.col_text(2);
        s.image_base64 = stmt.col_text(3);
        // Handle nullable REAL columns — NULL means "not yet judged"
        if (sqlite3_column_type(stmt.get(), 4) != SQLITE_NULL) {
            s.score = static_cast<float>(stmt.col_double(4));
        }
        s.feedback     = stmt.col_text(5);
        if (sqlite3_column_type(stmt.get(), 6) != SQLITE_NULL) {
            s.confidence = static_cast<float>(stmt.col_double(6));
        }
        s.submitted_at = stmt.col_text(7);
        submissions.push_back(s);
    }

    return submissions;
}

std::vector<SubmissionHistory> DatabaseManager::GetRecentSubmissions(const std::string& player_id, int limit) {
    std::vector<SubmissionHistory> history;

    StmtGuard stmt(db_,
        "SELECT r.prompt, s.score, s.feedback, s.image_base64, s.submitted_at "
        "FROM submissions s "
        "JOIN rounds r ON s.round_id = r.id "
        "WHERE s.player_id = ? "
        "ORDER BY s.submitted_at DESC "
        "LIMIT ?");
    
    if (!stmt.ok()) return history;

    stmt.bind_text(1, player_id);
    stmt.bind_int(2, limit);

    while (stmt.step()) {
        SubmissionHistory sh;
        sh.prompt       = stmt.col_text(0);
        if (sqlite3_column_type(stmt.get(), 1) != SQLITE_NULL) {
            sh.score = static_cast<float>(stmt.col_double(1));
        }
        sh.feedback     = stmt.col_text(2);
        sh.image_base64 = stmt.col_text(3);
        sh.submitted_at = stmt.col_text(4);
        history.push_back(sh);
    }

    return history;
}

} // namespace drawfusion
