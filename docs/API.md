# Internal API Reference

DrawFusion relies heavily on internal APIs for microservice communication. The primary interface is the gRPC service connecting the C++ Game Server and the Python AI Service.

## gRPC Service: `GameMaster`

The AI Service acts as the brain behind the game, exposing the `GameMaster` service defined in `proto/game-ai.proto`.

### Key Endpoints

- **`GetPrompt(PromptRequest)`**
  Generates a creative drawing prompt. It takes the game ID and difficulty, and returns a prompt (e.g., "Draw a fast car") and its category.

- **`JudgeRound(JudgeRoundRequest)`**
  The core judging mechanism. The C++ server sends a batch of player submissions (base64 images). The AI service evaluates them against the prompt and each other, returning scores, ranks, and personalized feedback.

- **`GetHint(HintRequest)`**
  Provides progressive contextual hints for struggling players based on the current prompt and time remaining.

- **`GetFeedback(FeedbackRequest)`**
  Requests detailed, constructive feedback (strengths and areas for improvement) for a specific drawing submission.

- **`HealthCheck(HealthCheckRequest)`**
  Used by the C++ backend on startup to verify that the AI service is online and serving requests.

## Client-Server Communication

Client-server communication is handled entirely via **WebSockets** (using `uWebSockets` on the C++ side).
- Payloads are JSON encoded.
- WebSockets handle real-time lobby updates, game state synchronization, and final image submission.
- Payload limits are configured up to 16MB to accommodate large base64 canvas exports without dropping connections during image submission.
