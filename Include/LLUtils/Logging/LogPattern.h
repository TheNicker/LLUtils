#pragma once
#include "LogOptions.h"
#include <charconv>
#include <array>
#include <format>
#include <stdexcept>
#include <vector>
#include <limits>

namespace LLUtils
{
    namespace LoggingDetail
    {
        // Parsing happens only when configuration is published. Message contents remain plain data.
        class Pattern
        {
            enum class Field
            {
                Literal,
                Date,
                Time,
                Module,
                Level,
                Message,
                Thread,
                File,
                Line,
                Function,
                Operation,
                Sequence
            };
            static constexpr unsigned MaxMetadataFieldWidth = 1024;
            static constexpr std::size_t DecimalCapacity    = std::numeric_limits<std::uint64_t>::digits10 + 1;
            // These specify known optimized layouts, not editable defaults. Other layouts use chrono formatting.
            static constexpr std::string_view CanonicalDate = "{:%Y-%m-%d}";
            static constexpr std::string_view CanonicalTime = "{:%H:%M:%S}";
            // One compiled literal/placeholder; rendering never interprets the message as a pattern.
            struct Part
            {
                Field field;       // Literal or metadata/message selector.
                std::string text;  // Owned literal text or chrono format specification for date/time.
                unsigned width =
                    LogDefaults::FieldWidth;           // Minimum metadata field width; never truncates longer values.
                char align = LogDefaults::FieldAlign;  // Padding direction: left, right or center.
                char fill  = LogDefaults::FieldFill;   // Padding character, independent of message contents.
            };

          public:

            explicit Pattern(LogTextFormat format) : fFormat(std::move(format))
            {
                if (static_cast<unsigned>(fFormat.timestampPrecision) >
                    static_cast<unsigned>(LogTimestampPrecision::Nanoseconds))
                    throw std::invalid_argument("Invalid log precision");
                std::string literal;
                const auto& pattern = fFormat.pattern;
                for (std::size_t i = 0; i < pattern.size();)
                {
                    const char ch = pattern[i];
                    if ((ch == '{' || ch == '}') && i + 1 < pattern.size() && pattern[i + 1] == ch)
                    {
                        literal += ch;
                        i += 2;
                    }
                    else if (ch == '{')
                    {
                        if (!literal.empty())
                        {
                            fParts.push_back({Field::Literal, std::move(literal)});
                            literal.clear();
                        }
                        const auto end = pattern.find('}', i + 1);
                        if (end == std::string::npos)
                            throw std::invalid_argument("Unclosed log field");
                        const auto token = std::string_view(pattern).substr(i + 1, end - i - 1);
                        if (token.find('{') != std::string_view::npos)
                            throw std::invalid_argument("Nested log field");
                        const auto colon = token.find(':');
                        const auto name  = token.substr(0, colon);
                        const auto field = Resolve(name);
                        Part part{field, {}};
                        const auto spec = colon == std::string_view::npos ? std::string_view{}
                                                                          : token.substr(colon + 1);
                        if (field == Field::Date || field == Field::Time)
                        {
                            part.text = "{:" +
                                        std::string(spec.empty() ? (field == Field::Date ? LogDefaults::DateSpec
                                                                                         : LogDefaults::TimeSpec)
                                                                 : spec) +
                                        "}";
                            if (!fFormat.utc && !fZone)
                                fZone = std::chrono::current_zone();
                            std::string sample;
                            Calendar(part, std::chrono::system_clock::time_point{}, sample);
                            fFields.set(LogFields::Timestamp);
                        }
                        else
                            ParseAlignment(part, spec);
                        if (field == Field::Thread)
                            fFields.set(LogFields::Thread);
                        if (field == Field::File || field == Field::Line || field == Field::Function)
                            fFields.set(LogFields::Source);
                        if (field == Field::Operation)
                            fFields.set(LogFields::Operation);
                        fParts.push_back(std::move(part));
                        i = end + 1;
                    }
                    else
                    {
                        if (ch == '}')
                            throw std::invalid_argument("Unmatched log brace");
                        literal += ch;
                        ++i;
                    }
                }
                if (!literal.empty())
                    fParts.push_back({Field::Literal, std::move(literal)});
            }
            LogFieldFlags Fields() const noexcept { return fFields; }
            const LogTextFormat& Format() const noexcept { return fFormat; }
            void RenderInto(const LogRecord& record, std::string& result) const
            {
                result.clear();
                std::array<char, DecimalCapacity> numeric;  // Largest uint64_t decimal representation.
                for (const auto& part : fParts)
                {
                    if (part.field == Field::Date || part.field == Field::Time)
                        Calendar(part, record.timestamp, result);
                    else
                    {
                        const auto text  = Value(part, record, numeric);
                        const auto count = part.width > text.size() ? part.width - text.size() : 0;
                        const auto left  = part.align == '>' ? count : part.align == '^' ? count / 2 : 0;
                        result.append(left, part.fill);
                        result += text;
                        result.append(count - left, part.fill);
                    }
                }
            }

          private:

            static Field Resolve(std::string_view name)
            {
                // Positional: entry i must name Field(i + 1), so Field order and this table cannot drift apart.
                constexpr std::string_view names[] = {"date", "time", "module",   "loglevel",    "message", "threadid",
                                                      "file", "line", "function", "operationid", "sequence"};
                static_assert(std::size(names) == 11, "one name per resolvable field, in Field declaration order");
                for (unsigned i = 0; i < std::size(names); ++i)
                    if (names[i] == name)
                        return static_cast<Field>(i + 1);
                throw std::invalid_argument("Unknown log field");
            }
            static void ParseAlignment(Part& part, std::string_view spec)
            {
                if (spec.empty())
                    return;
                auto alignment    = [](char c) { return c == '<' || c == '>' || c == '^'; };
                std::size_t start = 0;
                if (spec.size() >= 2 && alignment(spec[1]))
                {
                    part.fill  = spec[0];
                    part.align = spec[1];
                    start      = 2;
                }
                else if (alignment(spec[0]))
                {
                    part.align = spec[0];
                    start      = 1;
                }
                const auto [end, error] = std::from_chars(spec.data() + start, spec.data() + spec.size(), part.width);
                if (error != std::errc{} || end != spec.data() + spec.size() || part.width > MaxMetadataFieldWidth)
                    throw std::invalid_argument("Invalid log alignment");
            }
            template <class Duration>
            void Calendar(const Part& part, std::chrono::system_clock::time_point timestamp, std::string& output) const
            {
                using namespace std::chrono;
                const auto time = floor<Duration>(timestamp);
                // The default layouts need only calendar arithmetic and integer formatting; avoid general chrono parsing.
                const auto local = fZone ? zoned_time(fZone, time).get_local_time().time_since_epoch()
                                         : time.time_since_epoch();
                if (part.text == CanonicalDate || part.text == "{:%F}")
                {
                    const year_month_day date(sys_days(floor<days>(local)));
                    std::format_to(std::back_inserter(output), "{:04}-{:02}-{:02}", int(date.year()),
                                   unsigned(date.month()), unsigned(date.day()));
                }
                else if (part.text == CanonicalTime || part.text == "{:%T}")
                {
                    const hh_mm_ss clock(local - floor<days>(local));
                    std::format_to(std::back_inserter(output), "{:02}:{:02}:{:02}", clock.hours().count(),
                                   clock.minutes().count(), clock.seconds().count());
                    if constexpr (hh_mm_ss<Duration>::fractional_width != 0)
                        std::format_to(std::back_inserter(output), ".{:0{}}", clock.subseconds().count(),
                                       hh_mm_ss<Duration>::fractional_width);
                }
                else if (fZone)
                {
                    const std::chrono::zoned_time local(fZone, time);
                    std::vformat_to(std::back_inserter(output), part.text, std::make_format_args(local));
                }
                else
                    std::vformat_to(std::back_inserter(output), part.text, std::make_format_args(time));
            }
            void Calendar(const Part& part, std::chrono::system_clock::time_point timestamp, std::string& output) const
            {
                // Chrono handles all seconds directives, precision, timezone offsets and DST from the captured time.
                using namespace std::chrono;
                switch (fFormat.timestampPrecision)
                {
                    case LogTimestampPrecision::Seconds:
                        Calendar<seconds>(part, timestamp, output);
                        break;
                    case LogTimestampPrecision::Milliseconds:
                        Calendar<milliseconds>(part, timestamp, output);
                        break;
                    case LogTimestampPrecision::Microseconds:
                        Calendar<microseconds>(part, timestamp, output);
                        break;
                    case LogTimestampPrecision::Nanoseconds:
                        Calendar<nanoseconds>(part, timestamp, output);
                        break;
                }
            }
            static std::string_view Value(const Part& part, const LogRecord& record,
                                          std::array<char, DecimalCapacity>& numeric)
            {
                const auto number = [&](auto value)
                {
                    const auto [end, error] = std::to_chars(numeric.data(), numeric.data() + numeric.size(), value);
                    return std::string_view(numeric.data(), end - numeric.data());
                };
                switch (part.field)
                {
                    case Field::Literal:
                        return part.text;
                    case Field::Module:
                        return record.category.Name();
                    case Field::Level:
                        return LogLevelName(record.level);
                    case Field::Message:
                        return record.message;
                    case Field::Thread:
                        return number(record.threadId);
                    case Field::File:
                        return record.source.file_name();
                    case Field::Line:
                        return number(record.source.line());
                    case Field::Function:
                        return record.source.function_name();
                    case Field::Operation:
                        return record.operationId.value ? number(record.operationId.value) : "-";
                    case Field::Sequence:
                        return record.sequence ? number(record.sequence) : "-";
                    case Field::Date:
                    case Field::Time:
                        break;  // Rendered directly into the output buffer.
                }
                return {};
            }
            LogTextFormat fFormat;     // Original settings used for sharing equivalent compiled layouts.
            std::vector<Part> fParts;  // Parsed tokens in rendering order.
            // Union of optional metadata referenced by these tokens.
            LogFieldFlags fFields = LogFields::None;
            // Borrowed standard timezone database entry, resolved once for local-time rendering.
            const std::chrono::time_zone* fZone = nullptr;
        };
    }  // namespace LoggingDetail
}  // namespace LLUtils
