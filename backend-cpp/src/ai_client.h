/**
 * @file ai_client.h
 * @brief GameMasterClient — gRPC client wrapper for the Python AI service.
 *
 * This class encapsulates all gRPC communication with the AI microservice.
 * The game server uses this to request prompts, judge drawings, get hints,
 * and retrieve feedback — without knowing anything about gRPC internals.
 *
 * Usage:
 *     GameMasterClient ai("localhost:50051");
 *     if (ai.IsHealthy()) {
 *         auto [prompt, category] = ai.GetPrompt("game-123", "medium");
 *     }
 */

#pragma once

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <tuple>
#include <vector>

#include <grpcpp/grpcpp.h>
#include "game-ai.grpc.pb.h"

namespace drawfusion {

/**
 * Configuration for the gRPC client connection.
 */
struct AIClientConfig {
    std::string target_address = "localhost:50051";
    int deadline_ms = 5000;       // Per-RPC timeout
    int max_retries = 3;          // Retry count on transient failures
    int retry_delay_ms = 500;     // Delay between retries
};

/**
 * Wraps the GameMaster gRPC stub with timeout, retry, and error handling.
 */
class GameMasterClient {
public:
    explicit GameMasterClient(const AIClientConfig& config = {});
    ~GameMasterClient() = default;

    // Non-copyable, movable
    GameMasterClient(const GameMasterClient&) = delete;
    GameMasterClient& operator=(const GameMasterClient&) = delete;
    GameMasterClient(GameMasterClient&&) = default;
    GameMasterClient& operator=(GameMasterClient&&) = default;

    // ── Health ──────────────────────────────────────────────────

    /**
     * Check if the AI service is reachable and serving.
     * @return true if the service responded with serving=true
     */
    bool IsHealthy();

    // ── RPCs ────────────────────────────────────────────────────

    /**
     * Request a drawing prompt from the AI service.
     * @return {prompt, category} or nullopt on failure
     */
    std::optional<std::tuple<std::string, std::string>> GetPrompt(
        const std::string& game_id,
        const std::string& difficulty = "medium",
        const std::vector<std::string>& past_prompts = {}
    );

    struct PlayerScore {
        float score;
        int rank;
        std::string feedback;
        float confidence;
    };

    /**
     * Submit a batch of drawings for comparative judging asynchronously.
     * The callback receives a map of player_id -> PlayerScore, or nullopt on failure.
     */
    void JudgeRoundAsync(
        const std::string& round_id,
        const std::string& prompt,
        const std::vector<std::pair<std::string, std::string>>& submissions,
        std::function<void(std::optional<std::map<std::string, PlayerScore>>)> callback
    );

    /**
     * Request a hint for a player during a round.
     * @return hint string or nullopt on failure
     */
    std::optional<std::string> GetHint(
        const std::string& game_id,
        const std::string& prompt,
        float time_remaining,
        int hint_number = 1
    );

    /**
     * Request detailed feedback on a drawing.
     * @return {feedback, strengths, improvements} or nullopt on failure
     */
    std::optional<std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>> GetFeedback(
        const std::string& player_id,
        const std::string& prompt,
        const std::string& image_base64,
        float score
    );

private:
    AIClientConfig config_;
    std::shared_ptr<grpc::Channel> channel_;
    std::unique_ptr<drawfusion::GameMaster::Stub> stub_;

    /**
     * Create a gRPC ClientContext with the configured deadline.
     */
    std::unique_ptr<grpc::ClientContext> MakeContext();

    /**
     * Log a gRPC error status.
     */
    void LogRpcError(const std::string& rpc_name, const grpc::Status& status);
};

} // namespace drawfusion
