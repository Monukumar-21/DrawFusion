# DrawFusion Architecture

DrawFusion follows a modern microservices architecture, separating game logic, artificial intelligence, and client interactions to ensure high performance and scalability.

## System Overview

The system consists of three main components:

1. **Frontend (Client)**
   - **Tech:** HTML5 Canvas, Vanilla JS/CSS.
   - **Role:** Handles real-time drawing, rendering the user interface, and communicating with the game server via WebSockets.
   - **Communication:** Sends base64-encoded canvas snapshots and game actions over WebSockets.

2. **Backend Server (Game Logic)**
   - **Tech:** C++20, uWebSockets, SQLite.
   - **Role:** The core game engine. Manages WebSocket connections, lobby state, game sessions, and data persistence using an embedded SQLite database.
   - **Communication:** Serves WebSocket clients and makes gRPC calls to the AI Service.

3. **AI Service (Game Master)**
   - **Tech:** Python 3, gRPC, LangChain, Groq API, Hugging Face.
   - **Role:** Acts as the game master. Generates creative prompts, evaluates drawings, and provides personalized feedback.
   - **Communication:** Exposes a gRPC interface (`GameMaster`) to the C++ Backend.

## Data Flow

1. **Lobby & Matchmaking:** Clients connect to the C++ Backend via WebSockets to join or create lobbies.
2. **Game Start:** The Backend requests a drawing prompt from the AI Service via gRPC (`GetPrompt`).
3. **Gameplay:** Players draw on their canvas. When time is up or submitted, the Frontend sends the image (base64) to the Backend.
4. **Judging:** The Backend batches submissions and calls the AI Service (`JudgeRound`) via gRPC.
5. **Results:** The AI Service evaluates the drawings and returns scores/feedback. The Backend broadcasts the results to the Frontend.
