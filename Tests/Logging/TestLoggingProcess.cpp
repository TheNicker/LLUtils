#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <LLUtils/Logging/Logger.h>
#include "../Support/LoggingFixtures.h"
#include "../Support/TempFolder.h"
#include <array>
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
#ifdef _WIN32
    #include <process.h>
#endif

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
#ifdef _WIN32
    const std::array<std::wstring, 3> arguments{helper.native(), std::filesystem::path(mode).native(),
                                                folder.path.native()};
    const std::array<const wchar_t*, 4> commandLine{arguments[0].c_str(), arguments[1].c_str(), arguments[2].c_str(),
                                                    nullptr};
    CHECK(_wspawnv(_P_WAIT, helper.c_str(), commandLine.data()) == 0);
#else
    const auto command = "\"" + helper.string() + "\" " + mode + " \"" + folder.path.string() + "\"";
    CHECK(std::system(command.c_str()) == 0);
#endif
    const auto content = ReadFile(folder.path / "emergency");
    if (std::string_view(mode) == "bound")
        CHECK(content == std::string(1024, 'x'));
    else if (std::string_view(mode) == "inherit")
        CHECK_THAT(content, ContainsSubstring("survives shutdown"));
}
