#include <LLUtils/Logging/LogFileSink.h>
#include <LLUtils/Logging/Logger.h>
#include <LLUtils/ScopedFileLock.h>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>
#include <fstream>
#ifndef _WIN32
    #include <spawn.h>
    #include <sys/wait.h>
#endif

namespace
{
#ifdef _WIN32
    bool VerifyUnicodeConsole()
    {
        const auto previousError = GetStdHandle(STD_ERROR_HANDLE);
        FreeConsole();
        if (!AllocConsole())
            return false;
        ShowWindow(GetConsoleWindow(), SW_HIDE);
        const auto buffer = CreateConsoleScreenBuffer(GENERIC_READ | GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                                      nullptr, CONSOLE_TEXTMODE_BUFFER, nullptr);
        bool correct      = false;
        if (buffer != INVALID_HANDLE_VALUE)
        {
            if (SetStdHandle(STD_ERROR_HANDLE, buffer) && SetConsoleOutputCP(437))
            {
                LLUtils::Logger::Emergency("\xce\xbb \xe2\x82\xac");
                wchar_t observed[3]{};
                DWORD count{};
                correct = ReadConsoleOutputCharacterW(buffer, observed, 3, {0, 0}, &count) &&
                          std::wstring_view(observed, count) == L"\u03bb \u20ac";
            }
            SetStdHandle(STD_ERROR_HANDLE, previousError);
            CloseHandle(buffer);
        }
        FreeConsole();
        return correct;
    }
#endif
    struct LateLog
    {
        ~LateLog() { LL_LOG(LLUtils::Logger::RegisterCategory("Late"), LLUtils::LogLevel::Info, "late {}", 1); }
    } late;
    bool HasPrivateHandle(const std::filesystem::path& folder)
    {
#ifdef _WIN32
        wchar_t path[4096];
        for (std::uintptr_t value = 4; value < 65536; value += 4)
        {
            const auto count = GetFinalPathNameByHandleW(reinterpret_cast<HANDLE>(value), path, 4096,
                                                         FILE_NAME_NORMALIZED);
            if (count && count < 4096 &&
                std::wstring_view(path, count).find(folder.filename().native()) != std::wstring_view::npos)
                return true;
        }
#else
        for (const auto& entry : std::filesystem::directory_iterator("/proc/self/fd"))
        {
            std::error_code error;
            const auto path = std::filesystem::read_symlink(entry.path(), error);
            if (!error && path.string().find(folder.filename().string()) != std::string::npos)
                return true;
        }
#endif
        return false;
    }
    bool RunChild(const std::filesystem::path& folder)
    {
        const auto executable = std::filesystem::path(LLUtils::PlatformUtility::GetExePath());
#ifdef _WIN32
        SECURITY_ATTRIBUTES security{sizeof(security), nullptr, TRUE};
        HANDLE read{}, write{};
        if (!CreatePipe(&read, &write, &security, 0))
            return false;
        SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
        STARTUPINFOW startup{sizeof(startup)};
        startup.dwFlags    = STARTF_USESTDHANDLES;
        startup.hStdOutput = write;
        startup.hStdError  = write;
        startup.hStdInput  = GetStdHandle(STD_INPUT_HANDLE);
        PROCESS_INFORMATION process{};
        auto command       = L"\"" + executable.native() + L"\" child \"" + folder.native() + L"\"";
        const bool started = CreateProcessW(nullptr, command.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr,
                                            nullptr, &startup, &process) != FALSE;
        CloseHandle(write);
        DWORD code = 1;
        if (started)
        {
            if (WaitForSingleObject(process.hProcess, 10000) == WAIT_TIMEOUT)
                TerminateProcess(process.hProcess, 99);
            GetExitCodeProcess(process.hProcess, &code);
            CloseHandle(process.hThread);
            CloseHandle(process.hProcess);
        }
        char output[128]{};
        DWORD count{};
        ReadFile(read, output, 127, &count, nullptr);
        CloseHandle(read);
        return started && code == 0 &&
               std::string_view(output, count).find("stdout inherited") != std::string_view::npos;
#else
        int channel[2];
        if (pipe(channel) != 0)
            return false;
        posix_spawn_file_actions_t actions;
        posix_spawn_file_actions_init(&actions);
        posix_spawn_file_actions_adddup2(&actions, channel[1], STDOUT_FILENO);
        posix_spawn_file_actions_addclose(&actions, channel[0]);
        std::string exe = executable.string(), path = folder.string();
        char* args[] = {exe.data(), const_cast<char*>("child"), path.data(), nullptr};
        pid_t child{};
        const bool started = posix_spawn(&child, exe.c_str(), &actions, nullptr, args, environ) == 0;
        posix_spawn_file_actions_destroy(&actions);
        close(channel[1]);
        int status = 1;
        if (started)
            while (waitpid(child, &status, 0) == -1 && errno == EINTR)
            {
            }
        char output[128]{};
        const auto count = read(channel[0], output, 127);
        close(channel[0]);
        return started && WIFEXITED(status) && WEXITSTATUS(status) == 0 && count > 0 &&
               std::string_view(output, count).find("stdout inherited") != std::string_view::npos;
#endif
    }
}  // namespace

int main(int argc, char** argv)
{
    if (argc != 3)
        return 2;
    const std::string_view mode(argv[1]);
    const std::filesystem::path folder(argv[2]);
#ifdef _WIN32
    if (mode == "console")
        return VerifyUnicodeConsole() ? 0 : 7;
#endif
    if (mode == "lock")
    {
        std::ofstream(folder / "lock-started") << "started";
        const LLUtils::ScopedFileLock lock(folder / "coordination-lock");
        std::ofstream(folder / "lock-acquired") << "acquired";
        return 0;
    }
    if (mode == "child")
    {
        const bool inherited = HasPrivateHandle(folder);
        std::fputs("stdout inherited\n", stdout);
        std::fflush(stdout);
        return inherited ? 1 : 0;
    }
    LLUtils::LoggerOptions options;
    options.emergencyPath = folder / "emergency";
    options.sinks.push_back({std::make_shared<LLUtils::FileLogSink>(
        LLUtils::LogFileOptions{.path = folder / "viewer", .rotationBytes = 32})});
    if (LLUtils::Logger::Initialize(std::move(options)) != LLUtils::LogResult::Success)
        return 3;
    const auto category = LLUtils::Logger::RegisterCategory("Process");
    for (unsigned i = 0; i < 4; ++i)
        LL_LOG(category, LLUtils::LogLevel::Info, "Record {} forces rotation", i);
    if (LLUtils::Logger::Flush() != LLUtils::LogResult::Success)
        return 4;
    if (mode == "inherit")
    {
        const LLUtils::ScopedFileLock lock(folder / "inherit-lock");
        if (!RunChild(folder))
            return 5;
    }
    if (LLUtils::Logger::Shutdown() != LLUtils::LogResult::Success)
        return 6;
    if (mode == "bound")
        LLUtils::Logger::Emergency(std::string(1024, 'x') + "beyond the emergency byte limit");
    else
        LLUtils::Logger::Emergency("Emergency descriptor survives shutdown\n");
    return 0;
}
