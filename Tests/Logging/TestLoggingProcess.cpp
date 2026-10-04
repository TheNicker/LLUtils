#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <LLUtils/Logging/Logger.h>
#include "../Support/LoggingFixtures.h"
#include "../Support/TempFolder.h"
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <future>
#include <latch>
#include <string>
#include <thread>
#include <vector>

namespace
{
    using namespace LLUtils;
    using namespace LLUtils::TestSupport;
    using namespace std::chrono_literals;
    using Catch::Matchers::ContainsSubstring;
}  // namespace

TEST_CASE("Emergency output bounds notices, preserves console Unicode and rejects descriptor inheritance",
          "[logging][process]")
{
    TempFolder folder;
    auto helper = std::filesystem::path(PlatformUtility::GetExePath()).parent_path() / "tests_logging_process";
#ifdef _WIN32
    helper += ".exe";
#endif
#ifdef _WIN32
    const auto mode = GENERATE("inherit", "bound", "console");
#else
    const auto mode = GENERATE("inherit", "bound");
#endif
    const auto command = "\"" + helper.string() + "\" " + mode + " \"" + folder.path.string() + "\"";
    // Generated paths contain no shell metacharacters. The helper uses native child creation, not a shell.
#ifdef _WIN32
    const auto shellCommand = "\"" + command + "\"";
    CHECK(std::system(shellCommand.c_str()) == 0);
#else
    CHECK(std::system(command.c_str()) == 0);
#endif
    const auto content = ReadFile(folder.path / "emergency");
    if (std::string_view(mode) == "bound")
        CHECK(content == std::string(1024, 'x'));
    else if (std::string_view(mode) == "inherit")
        CHECK_THAT(content, ContainsSubstring("survives shutdown"));
}
