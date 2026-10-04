#pragma once
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <string>

namespace LLUtils::TestSupport
{
    // A uniquely named temporary directory removed on destruction, so file rotation, retention and
    // descriptor behavior can be asserted without touching anything outside the system temp directory.
    struct TempFolder
    {
        std::filesystem::path path = std::filesystem::temp_directory_path() /
                                     ("llutils-test-" +
                                      std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        TempFolder() { std::filesystem::create_directories(path); }
        ~TempFolder()
        {
            std::error_code error;
            std::filesystem::remove_all(path, error);
        }
    };
    // Whole-file binary read; a missing or unreadable path yields an empty string.
    inline std::string ReadFile(const std::filesystem::path& path)
    {
        std::ifstream stream(path, std::ios::binary);
        return {std::istreambuf_iterator<char>{stream}, {}};
    }
}  // namespace LLUtils::TestSupport