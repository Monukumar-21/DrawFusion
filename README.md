# DrawFusion

**⚡ A high-performance, AI-driven multiplayer drawing experience powered by C++ and Python.**

DrawFusion is a distributed multiplayer system that combines a lightning-fast C++ backend with a Python-based AI microservice. Players join lobbies, receive AI-generated drawing prompts, and compete by drawing on a canvas, while an intelligent AI agent evaluates their submissions in real-time.

## Why This Project? (My Learning Journey)

DrawFusion is not just a game; it is my hands-on deep dive into distributed systems engineering. By choosing to build the core game server in C++ rather than a more forgiving environment like Node.js, I set out to master:
- **Low-Level Resource Management**: Avoiding memory leaks using RAII patterns (like my custom `StmtGuard` for SQLite) instead of relying on a garbage collector.
- **Asynchronous Networking**: Implementing non-blocking WebSocket architectures and asynchronous gRPC callback pipelines to prevent thread starvation during heavy ML workloads.
- **Microservice Interoperability**: Bridging the gap between a high-performance C++ core and a Python-based ML ecosystem, mimicking how enterprise applications scale AI workflows.
- **Modern Build Systems**: Conquering the steep learning curve of `CMake` and `vcpkg` to manage heavy dependencies like gRPC and Protobuf in a C++20 environment.

## Key Features

- **Distributed Architecture**: Separation of concerns between high-speed game logic (C++) and complex ML inference (Python).
- **Language Interoperability**: Seamless, high-speed gRPC communication between the C++ performance core and the Python AI service.
- **Robust Persistence**: Zero-config, file-based SQLite database with a custom C++ wrapper for fast, memory-safe data management.
- **Real-Time Multiplayer**: Low-latency WebSocket integration capable of handling real-time multiplayer matchmaking and game state synchronization.
- **Active AI Participant**: An AI agent capable of generating creative drawing prompts, analyzing the game state, scoring player drawings, and providing constructive feedback.

## System Architecture

The project is structured around a microservices pattern:

1. **Frontend (Planned)**: A web-based canvas UI.
2. **C++ Game Server**: Handles real-time WebSocket connections, lobby management, game state, and database interactions. Built for raw performance and scalability.
3. **Python AI Service**: A gRPC server running LangChain/LangGraph agents and ML models (like CLIP) to handle prompt generation and drawing evaluation.

## Current Milestones Completed

### 1. C++ Build System & Environment
- Established a robust C++20 build pipeline using **CMake** and **vcpkg** (manifest mode).
- Successfully integrated 8 core dependencies, including gRPC, Protobuf, uWebSockets, and SQLite3.

### 2. gRPC Microservice Integration
- Defined the system contract in `game-ai.proto` with 5 distinct RPCs.
- Implemented a resilient C++ `GameMasterClient` that auto-generates stubs, handles timeouts, and ensures graceful degradation if the AI service goes offline.

### 3. Database Layer (SQLite3)
- Implemented a full relational schema covering Users, Lobbies, Game Sessions, Rounds, and Submissions.
- Built a custom `DatabaseManager` in C++ with parameterized queries to prevent SQL injection.
- Utilized RAII patterns (`StmtGuard`) for automatic memory management of SQLite statements, preventing memory leaks.
- Replaced legacy PostgreSQL dependencies with a lightweight, embedded SQLite3 engine for simplified deployment.

### 4. WebSocket Server
- Integrated `uWebSockets` to manage high-throughput, real-time client communication.
- Implemented JSON-based message routing with graceful error handling and signal-based shutdown.

## Getting Started

### Prerequisites
- GCC (13.3.0+) or equivalent C++20 compiler
- CMake (3.20+)
- vcpkg
- Python 3.10+ (for the AI service)

### Building the Server

```bash
cd backend-cpp
# Configure the build system (using vcpkg toolchain)
cmake -B build -DCMAKE_TOOLCHAIN_FILE=/path/to/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release
# Compile the project
cmake --build build -j$(nproc)
```

### Running Tests

```bash
cd backend-cpp
# Verify database operations (CRUD, schema migration, UUID generation)
./build/test_db
# Verify library integration (gRPC, SQLite, JSON, JWT, etc.)
./build/test_libs
```

### Starting the Server

```bash
./backend-cpp/build/drawfusion_server
```
*Note: The server will start on WebSocket port 9001 and attempt to connect to the AI gRPC service on port 50051.*