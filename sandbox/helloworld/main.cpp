#include <iostream>
#include <spdlog/spdlog.h> // Import the new library!

int main() {
    // Standard C++ print
    std::cout << "Hello, World! This is standard C++." << std::endl;

    // Fancy colorized print from the third-party library
    spdlog::info("Hello, World! This is spdlog in action!");
    spdlog::warn("It even supports colorized warnings...");
    spdlog::error("...and errors out of the box!");

    return 0;
}
