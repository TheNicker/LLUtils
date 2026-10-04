#pragma once
#include "LogRecord.h"
#include <memory>
#include <unordered_map>
#include <vector>

namespace LLUtils::LoggingDetail
{
    // The compiled layout is only defined for publishers and the writer; this model only shares ownership.
    class Pattern;

    // Immutable delivery requirements for one sink; actual sink state belongs to the writer.
    struct Destination
    {
        // Combined with the effective global/category filter for live delivery and automatic replay selection.
        // Manual replay bypasses level thresholds, but Off always excludes this sink.
        LogLevel sinkMinimumLevel;
        std::shared_ptr<const Pattern> pattern;  // Shared compiled text layout.
        LogFieldFlags fields{LogFields::None};     // Raw sink requirements, separate from pattern fields.
        bool text;                                 // NeedsText declaration captured at initialization.
        bool overridden;                           // Explicit layout must not follow later default-layout updates.
    };
    // Published atomically and never mutated; admitted commands retain their selected snapshot.
    struct Configuration
    {
        // Default live filter, replaced by a category override; each selected sink adds its own filter.
        LogLevel globalMinimumLevel;
        std::unordered_map<std::uint64_t, LogLevel>
            categories;  // Replacements for globalMinimumLevel by category ID; explicit Off also disables history.
        std::shared_ptr<const Pattern> defaultPattern;  // Shared by non-overridden sinks.
        std::vector<Destination> destinations;          // Indexes match the fixed session sink list.
    };
    inline bool Eligible(LogLevel level, LogLevel minimumLevel)
    {
        return minimumLevel != LogLevel::Off && level >= minimumLevel;
    }
    inline LogLevel EffectiveCategoryMinimumLevel(const Configuration& config, const LogCategory& category)
    {
        const auto found = config.categories.find(category.Identity());
        return found == config.categories.end() ? config.globalMinimumLevel : found->second;
    }
    // The single live and replay admission rule, shared by producer field capture and writer delivery. If the two
    // ever disagreed, a sink could receive a record whose metadata was never captured, so neither side may
    // restate it. Callers running a destination loop hoist EffectiveCategoryMinimumLevel and pass it here
    // instead of repeating the category lookup per sink. manualReplay selects an explicit history dump.
    inline bool SelectsDestination(const Destination& destination, LogLevel level, LogLevel categoryMinimumLevel,
                                   bool manualReplay = false)
    {
        return destination.sinkMinimumLevel != LogLevel::Off &&
               (manualReplay ||
                (Eligible(level, categoryMinimumLevel) && Eligible(level, destination.sinkMinimumLevel)));
    }
}  // namespace LLUtils::LoggingDetail