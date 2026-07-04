# DrawFusion
**⚡ A high-performance, AI-driven multiplayer drawing experience powered by C++ and Python.**

Transitioning from Python to the C++ development environment is a steep learning curve, but it is essential for understanding how modern AI/ML frameworks achieve blistering high-speed execution and efficient memory management. Instead of relying on standard tutorials, this project focuses on hands-on learning by building the foundational backend architecture for DrawFusion.

This phase will bridge the gap between high-level AI concepts and low-level system performance. By combining a lightning-fast C++ backend with a Python-based AI microservice, I will build a distributed multiplayer system that features an intelligent, multi-functional AI agent.
Key Objectives
Language Interoperability: Establish seamless, high-speed communication betIen the C++ performance core and the Python interface.

Distributed Backend: Design a robust server architecture capable of handling real-time multiplayer matchmaking, game state synchronization, and networking.

Active AI Participant: Develop an AI agent that can join lobbies and play the game alongside human users.

Dynamic Game Management: Enable the AI to generate creative drawing prompts and accurately evaluate players' guessed ansIrs.

Intelligent Hint & Feedback System: Program the AI to analyze the game state in real-time to provide constructive feedback and contextual hints to players.

## Milestone 1 : Development in C++

### What I Learned: Python vs C++ Environments
Today, I successfully navigated the steep learning curve of setting up a C++ project from scratch. Coming from a Python background, here are the major conceptual shifts I mastered:

1. **Package Management (`vcpkg` vs `pip`)**: 
   Unlike Python where `pip` simply downloads pre-packaged code, C++ requires downloading source code and compiling it locally for the specific operating system. I used `vcpkg` in manifest mode (`vcpkg.json`), which acts as our `requirements.txt`.

2. **The Build System (`CMake` vs `setup.py`)**: 
   C++ does not automatically know where installed libraries are located. I learned how to use `CMakeLists.txt` to act as our blueprint. It finds our dependencies (`find_package`), generates our gRPC network stubs dynamically from `.proto` files, and bundles our code together.

3. **Compilation vs Interpretation (`g++` vs `python`)**: 
   Instead of running our code "on the fly" through an interpreter, I used CMake's build command (`cmake --build build`) to permanently translate our human-readable `.cpp` files into raw, standalone machine code (ELF binaries). 

4. **Executable Binaries**: 
   I proved that once the code is compiled, I don't need a compiler or interpreter to run it. The resulting binary (like our `test_libs` smoke test) can run natively on the operating system at incredibly high speeds.