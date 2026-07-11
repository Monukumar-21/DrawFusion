/**
 * @file ai_client.cpp
 * @brief Implementation of GameMasterClient — gRPC calls to Python AI service.
 */

#include "ai_client.h"

#include <chrono>
#include <thread>

#include <spdlog/spdlog.h>

namespace drawfusion {

GameMasterClient::GameMasterClient(const AIClientConfig& config)
    : config_(config)
{
    // Create channel with production-grade options
    grpc::ChannelArguments args;
    args.SetMaxSendMessageSize(50 * 1024 * 1024);      // 50 MB
    args.SetMaxReceiveMessageSize(50 * 1024 * 1024);    // 50 MB
    args.SetInt(GRPC_ARG_KEEPALIVE_TIME_MS, 30000);     // Ping every 30s
    args.SetInt(GRPC_ARG_KEEPALIVE_TIMEOUT_MS, 10000);  // Wait 10s for pong
    args.SetInt(GRPC_ARG_KEEPALIVE_PERMIT_WITHOUT_CALLS, 1);

    channel_ = grpc::CreateCustomChannel(
        config_.target_address,
        grpc::InsecureChannelCredentials(),
        args
    );
    stub_ = drawfusion::GameMaster::NewStub(channel_);

    spdlog::info("[AI Client] Initialized → target={}, deadline={}ms, retries={}",
        config_.target_address, config_.deadline_ms, config_.max_retries);
}

std::unique_ptr<grpc::ClientContext> GameMasterClient::MakeContext() {
    auto ctx = std::make_unique<grpc::ClientContext>();
    auto deadline = std::chrono::system_clock::now()
        + std::chrono::milliseconds(config_.deadline_ms);
    ctx->set_deadline(deadline);
    return ctx;
}

void GameMasterClient::LogRpcError(const std::string& rpc_name, const grpc::Status& status) {
    spdlog::error("[AI Client] {} failed: code={} message='{}'",
        rpc_name,
        static_cast<int>(status.error_code()),
        status.error_message()
    );
}

bool GameMasterClient::IsHealthy() {
    drawfusion::HealthCheckRequest request;
    drawfusion::HealthCheckResponse response;
    auto context = MakeContext();

    auto status = stub_->HealthCheck(context.get(), request, &response);

    if (status.ok() && response.serving()) {
        spdlog::info("[AI Client] Health check passed — AI service v{}", response.version());
        return true;
    }

    if (!status.ok()) {
        LogRpcError("HealthCheck", status);
    } else {
        spdlog::warn("[AI Client] Health check: service not serving");
    }
    return false;
}

std::optional<std::string> GameMasterClient::ValidateKeys(const std::string& groq_key) {
    drawfusion::ValidateKeysRequest request;
    request.set_groq_key(groq_key);

    drawfusion::ValidateKeysResponse response;
    auto context = MakeContext();

    auto status = stub_->ValidateKeys(context.get(), request, &response);

    if (status.ok()) {
        if (response.is_valid()) {
            return std::nullopt; // No error
        } else {
            return response.error_message();
        }
    }

    LogRpcError("ValidateKeys", status);
    return "Failed to contact AI service for validation.";
}

std::optional<std::tuple<std::string, std::string>> GameMasterClient::GetPrompt(
    const std::string& game_id,
    const std::string& difficulty,
    const std::vector<std::string>& past_prompts,
    const std::string& custom_groq_key
) {
    drawfusion::PromptRequest request;
    request.set_game_id(game_id);
    request.set_difficulty(difficulty);
    if (!custom_groq_key.empty()) request.set_custom_groq_key(custom_groq_key);
    for (const auto& p : past_prompts) {
        request.add_past_prompts(p);
    }

    for (int attempt = 0; attempt <= config_.max_retries; ++attempt) {
        drawfusion::PromptResponse response;
        auto context = MakeContext();

        auto status = stub_->GetPrompt(context.get(), request, &response);

        if (status.ok()) {
            spdlog::info("[AI Client] GetPrompt: '{}' [{}]",
                response.prompt(), response.category());
            return std::make_tuple(response.prompt(), response.category());
        }

        LogRpcError("GetPrompt", status);

        // Retry on transient errors only
        if (status.error_code() == grpc::StatusCode::UNAVAILABLE ||
            status.error_code() == grpc::StatusCode::DEADLINE_EXCEEDED) {
            if (attempt < config_.max_retries) {
                spdlog::warn("[AI Client] Retrying GetPrompt ({}/{})",
                    attempt + 1, config_.max_retries);
                std::this_thread::sleep_for(
                    std::chrono::milliseconds(config_.retry_delay_ms));
                continue;
            }
        }
        break;
    }

    return std::nullopt;
}

std::optional<std::map<std::string, GameMasterClient::PlayerScore>> GameMasterClient::JudgeRound(
    const std::string& round_id,
    const std::string& prompt,
    const std::vector<std::pair<std::string, std::string>>& submissions,
    const std::string& custom_groq_key
) {
    drawfusion::JudgeRoundRequest request;
    request.set_round_id(round_id);
    request.set_prompt(prompt);
    if (!custom_groq_key.empty()) request.set_custom_groq_key(custom_groq_key);

    for (const auto& [pid, img] : submissions) {
        auto* sub = request.add_submissions();
        sub->set_player_id(pid);
        sub->set_image_base64(img);
    }

    drawfusion::JudgeRoundResponse response;
    auto context = MakeContext();
    
    // Longer deadline for batched image processing
    auto deadline = std::chrono::system_clock::now()
        + std::chrono::milliseconds(config_.deadline_ms * 3);
    context->set_deadline(deadline);

    auto status = stub_->JudgeRound(context.get(), request, &response);

    if (status.ok()) {
        std::map<std::string, PlayerScore> results;
        for (const auto& r : response.results()) {
            results[r.player_id()] = {r.score(), r.rank(), r.feedback(), r.confidence()};
        }
        spdlog::info("[AI Client] JudgeRound completed for round {}: {} scores", 
            round_id, results.size());
        return results;
    }

    LogRpcError("JudgeRound", status);
    return std::nullopt;
}

void GameMasterClient::JudgeRoundAsync(
    const std::string& round_id,
    const std::string& prompt,
    const std::vector<std::pair<std::string, std::string>>& submissions,
    std::function<void(std::optional<std::map<std::string, PlayerScore>>)> callback,
    const std::string& custom_groq_key
) {
    auto request = std::make_unique<drawfusion::JudgeRoundRequest>();
    request->set_round_id(round_id);
    request->set_prompt(prompt);
    if (!custom_groq_key.empty()) request->set_custom_groq_key(custom_groq_key);

    for (const auto& [pid, img] : submissions) {
        auto* sub = request->add_submissions();
        sub->set_player_id(pid);
        sub->set_image_base64(img);
    }

    auto response = std::make_unique<drawfusion::JudgeRoundResponse>();
    auto context = MakeContext();

    // Longer deadline for batched image processing
    auto deadline = std::chrono::system_clock::now()
        + std::chrono::milliseconds(config_.deadline_ms * 3);
    context->set_deadline(deadline);

    // Release pointers to pass into the callback, so they live until the RPC completes
    auto req_ptr = request.release();
    auto res_ptr = response.release();
    auto ctx_ptr = context.release();

    stub_->async()->JudgeRound(ctx_ptr, req_ptr, res_ptr,
        [this, req_ptr, res_ptr, ctx_ptr, cb = std::move(callback)](grpc::Status status) {
            // Re-take ownership to ensure cleanup
            std::unique_ptr<drawfusion::JudgeRoundRequest> req_guard(req_ptr);
            std::unique_ptr<drawfusion::JudgeRoundResponse> res_guard(res_ptr);
            std::unique_ptr<grpc::ClientContext> ctx_guard(ctx_ptr);

            if (status.ok()) {
                std::map<std::string, PlayerScore> results;
                for (const auto& r : res_guard->results()) {
                    results[r.player_id()] = {r.score(), r.rank(), r.feedback(), r.confidence()};
                }
                spdlog::info("[AI Client] JudgeRound completed for round {}: {} scores", 
                    req_guard->round_id(), results.size());
                cb(std::move(results));
            } else {
                LogRpcError("JudgeRoundAsync", status);
                cb(std::nullopt);
            }
        });
}

std::optional<std::string> GameMasterClient::GetHint(
    const std::string& game_id,
    const std::string& prompt,
    float time_remaining,
    int hint_number,
    const std::string& custom_groq_key
) {
    drawfusion::HintRequest request;
    request.set_game_id(game_id);
    request.set_prompt(prompt);
    request.set_time_remaining(time_remaining);
    request.set_hint_number(hint_number);
    if (!custom_groq_key.empty()) request.set_custom_groq_key(custom_groq_key);

    drawfusion::HintResponse response;
    auto context = MakeContext();

    auto status = stub_->GetHint(context.get(), request, &response);

    if (status.ok()) {
        spdlog::info("[AI Client] GetHint #{}: '{}'", hint_number, response.hint());
        return response.hint();
    }

    LogRpcError("GetHint", status);
    return std::nullopt;
}

std::optional<std::tuple<std::string, std::vector<std::string>, std::vector<std::string>>>
GameMasterClient::GetFeedback(
    const std::string& player_id,
    const std::string& prompt,
    const std::string& image_base64,
    float score
) {
    drawfusion::FeedbackRequest request;
    request.set_player_id(player_id);
    request.set_prompt(prompt);
    request.set_image_base64(image_base64);
    request.set_score(score);

    drawfusion::FeedbackResponse response;
    auto context = MakeContext();

    auto status = stub_->GetFeedback(context.get(), request, &response);

    if (status.ok()) {
        std::vector<std::string> strengths(
            response.strengths().begin(), response.strengths().end());
        std::vector<std::string> improvements(
            response.improvements().begin(), response.improvements().end());

        spdlog::info("[AI Client] GetFeedback: player={} strengths={} improvements={}",
            player_id, strengths.size(), improvements.size());
        return std::make_tuple(response.feedback(), strengths, improvements);
    }

    LogRpcError("GetFeedback", status);
    return std::nullopt;
}

} // namespace drawfusion
