#include <LLUtils/Logging/Logger.h>
#include <iostream>
#include <thread>
#include <vector>

namespace
{
    class Counter final : public LLUtils::LogSink
    {
      public:

        LLUtils::LogFieldFlags fields = LLUtils::LogFields::None;
        bool text                     = false;
        bool NeedsText() const noexcept override { return text; }
        LLUtils::LogFieldFlags RequiredFields() const noexcept override { return fields; }
        bool Write(const LLUtils::LogDelivery&) override
        {
            ++records;
            return true;
        }
        bool Flush() override { return true; }
        std::size_t records = 0;
    };
}  // namespace
int main()
{
    using namespace LLUtils;
    using Clock              = std::chrono::steady_clock;
    constexpr unsigned Calls = 100000;
    const auto category      = Logger::RegisterCategory("Benchmark");
    for (const auto name : {"disabled", "preformatted", "formatted", "metadata", "text", "history", "concurrent"})
    {
        auto sink = std::make_shared<Counter>();
        LoggerOptions options;
        options.format.pattern = "{message}";
        options.flushInterval  = std::chrono::hours(1);
        if (name == std::string_view("disabled"))
            options.globalMinimumLevel = LogLevel::Off;
        if (name == std::string_view("metadata"))
            sink->fields = LogFields::All;
        if (name == std::string_view("text"))
        {
            sink->text             = true;
            options.format.pattern = "[{date}][{time}][{threadid}][{operationid}]{message}";
        }
        if (name == std::string_view("history"))
            options.history.enabled = true;
        options.sinks.push_back({sink, LogLevel::Info});
        if (Logger::Initialize(std::move(options)) != LogResult::Success)
            return 1;
        const auto start   = Clock::now();
        const auto produce = [&](unsigned count)
        {
            const OperationScope operation(OperationId::Create());
            for (unsigned i = 0; i < count; ++i)
            {
                if (name == std::string_view("preformatted"))
                    Logger::Log(category, LogLevel::Info, "ready message");
                else
                    LL_LOG(category, name == std::string_view("history") ? LogLevel::Debug : LogLevel::Info,
                           "message {}", i);
            }
        };
        if (name == std::string_view("concurrent"))
        {
            std::vector<std::jthread> threads;
            for (unsigned i = 0; i < 8; ++i)
                threads.emplace_back(produce, Calls / 8);
        }
        else
            produce(Calls);
        const auto admissionDone = Clock::now();
        if (Logger::Flush() != LogResult::Success)
            return 2;
        const auto drainDone = Clock::now();
        std::cout << name << ": " << std::chrono::duration<double, std::nano>(admissionDone - start).count() / Calls
                  << " ns/call wall time; drain "
                  << std::chrono::duration<double, std::milli>(drainDone - admissionDone).count()
                  << " ms; live records " << sink->records << '\n';
        if (Logger::Shutdown() != LogResult::Success)
            return 3;
    }
    std::atomic<std::shared_ptr<const unsigned>> snapshot(std::make_shared<const unsigned>(1));
    unsigned consumed = 0;
    const auto start  = Clock::now();
    for (unsigned i = 0; i < Calls; ++i)
        consumed += *snapshot.load();
    std::cout << "atomic shared snapshot microbenchmark: "
              << std::chrono::duration<double, std::nano>(Clock::now() - start).count() / Calls << " ns/load; consumed "
              << consumed << '\n';
}
