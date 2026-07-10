# Database Schema

DrawFusion uses an embedded **SQLite** database managed by the C++ Backend. The schema is designed to track users, multiplayer lobbies, and game sessions efficiently.

## Core Entities

- **users**: Stores registered players. Includes unique identifiers (UUIDs), usernames, emails, and securely hashed passwords.
- **lobbies**: Represents waiting rooms before a game starts. Lobbies have short join codes (e.g., "A7X9K2") and track state (`waiting`, `started`, `expired`).
- **lobby_players**: A junction table linking `users` to `lobbies`. It also supports AI bot players.
- **game_sessions**: Represents an active or completed game spawned from a lobby.
- **rounds**: Tracks individual drawing rounds within a game session, including the prompt (e.g., "Draw a cat") and the time limit.
- **submissions**: Stores the actual player submissions per round. Includes the base64 image data, AI-generated score, and personalized feedback.

## Key Relationships

- A **User** can host many **Lobbies** and participate in many **Lobby Players**.
- A **Lobby** transitions into a **Game Session**.
- A **Game Session** consists of multiple **Rounds**.
- A **Round** receives multiple **Submissions** (one per player).

*Note: The system utilizes `ON DELETE CASCADE` to automatically clean up associated lobbies, matches, and submissions when a user account is deleted.*
