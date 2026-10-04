# LLUtils Logging System

This folder contains the core logging pipeline used by LLUtils. The design separates producer-side logging from the writer thread that performs filtering, formatting, delivery, history retention and flush coordination.

## High-level model

The logging system is built around a few key ideas:

- A log call is captured as a `LogRecord` with category, level, message, source metadata and optional operation context.
- Producers enqueue records into a bounded queue instead of writing directly to sinks.
- A dedicated writer thread consumes the queue and performs:
  - category / severity filtering
  - pattern rendering
  - sink delivery
  - history retention
  - periodic or explicit flushes
- Each sink is independent and can have its own minimum level and formatting.

This keeps the fast path cheap for producers and ensures all output decisions happen on one serialized writer thread.

## Main components

### 1. Logger

Defined in `Logger.h`.

`Logger` is the public session API. It owns:

- global logging configuration for one runtime session
- category registration and lookup
- session start/shutdown lifecycle
- queue admission for log entries and control commands
- the writer worker thread
- exception hooks and emergency reporting

Typical public operations include:

- `Initialize(...)`
- `Shutdown()`
- `Flush()`
- `DumpHistory(...)`
- `RegisterCategory(...)`
- `FindCategory(...)`
- `Emergency(...)`

The logger is intentionally session-oriented: one process can have a single active runtime and all producers share the same queue and sink graph.

### 2. LogOptions and LogDefaults

Defined in `LogOptions.h` and `OperationContext.h`.

These types define the runtime policy for a logging session:

- default global minimum level
- queue size and per-message byte limits
- flush interval and flush severity
- default text format
- history settings
- sink list and sink-level configuration
- file rotation and retention policy

`LogDefaults` provides the built-in defaults, while `LoggerOptions` is the runtime configuration object passed to `Logger::Initialize(...)`.

### 3. LogRecord and LogDelivery

Defined in `LogRecord.h`.

`LogRecord` is the core data structure for a log event. It contains:

- category (`LogCategory`)
- severity (`LogLevel`)
- message text
- timestamp
- producing thread ID
- source location (`std::source_location`)
- operation identifier (`OperationId`)
- generated sequence number
- optional suppressed-count metadata
- captured fields mask

`LogDelivery` is the data handed to sinks during output. It includes the record itself plus the rendered text and flags indicating whether the record is a replay/history item or a generated marker.

`LogSink` is the abstract sink interface. Every sink implements:

- `NeedsText()`
- `RequiredFields()`
- `Write(const LogDelivery&)`
- `Flush()`

This allows sinks to request only the metadata they need and to opt out of text rendering when the sink only cares about structured data.

### 4. LogPattern

Defined in `LogPattern.h`.

`Pattern` compiles and renders a log text layout. It supports placeholders such as:

- `{date}`
- `{time}`
- `{module}`
- `{loglevel}`
- `{message}`
- `{threadid}`
- `{file}`
- `{line}`
- `{function}`
- `{operationid}`
- `{sequence}`

Patterns can include literal text and alignment/width controls. The compiled pattern is then reused for efficient rendering. Pattern field requirements are tracked so the writer captures only data that sinks actually need.

### 5. LogConfiguration

Defined in `LogConfiguration.h`.

This file defines the runtime configuration snapshot used by the writer. The important pieces are:

- `Destination`: a sink-specific filter and format configuration
- `Configuration`: the global filter, category overrides, default pattern and destination list
- `Eligible(...)`
- `EffectiveCategoryMinimumLevel(...)`
- `SelectsDestination(...)`

The logger publishes immutable configuration snapshots to the writer. This ensures that a sink sees a coherent combination of filters and layouts while the application may still update logger configuration elsewhere.

### 6. LogWriter

Defined in `LogWriter.h`.

`LogWriter` is the serialization engine. It owns:

- sink state and enabled/disabled tracking
- history ring state
- rendering buffers
- filter checks and replay logic
- flush throttling and sink disabling on failure

It processes queue commands:

- `DataCommand`: regular log records
- `FlushCommand`: ordered flush barrier
- `DumpCommand`: manual history replay

The writer thread is responsible for deciding whether a record should be rendered, retained in history, replayed, or suppressed. It also flushes sinks when configured by severity and interval.

### 7. Concrete sinks

#### Console sink

Defined in `LogConsoleSink.h`.

`ConsoleLogSink` writes to standard error.

On Windows, it detects whether the console is attached and uses `WriteConsoleW`; otherwise it falls back to UTF-8 output to stderr. This keeps developer logs usable both in consoles and redirected output.

#### Debug sink

Also in `LogConsoleSink.h`.

`DebugLogSink` emits to the Windows debugger with `OutputDebugStringW` and is a no-op on non-Windows platforms.

#### File sink

Defined in `LogFileSink.h`.

`FileLogSink` writes records to one or more generated session files. Features include:

- named session segments
- per-segment rotation based on `rotationBytes`
- CRLF-normalized record output
- retention cleanup for closed files
- process/session-scoped naming to avoid collisions
- file locking while enumerating/retaining files

This sink is well-suited for persistent diagnostic output.

### 8. Emergency reporting

Defined in `../Emergency.h` (namespace `LLUtils::EmergencyDetail`) and, for the private descriptor,
`LogEmergencyFile.h` in this folder.

The emergency subsystem is designed to remain usable even when the normal logging stack is failing, re-entering, or being torn down. It avoids heap allocation and does not depend on the active logger state.

Key behaviors:

- recursive logging is discarded
- emergency output is written to a private file descriptor when configured
- messages are also written to stderr/debugger
- the emergency file is kept out of child-process inheritance
- emergency reporting is intentionally conservative and can be used from a faulting thread or shutdown path
- `Emergency::Active()` reports whether a diagnostic pipeline is running on the calling thread. `Exception.h` uses it to suppress observer notification for an exception constructed inside that pipeline, which is why the reporter is not logging-specific: it must not name a logger, and it lives outside this folder for that reason.

This is the last-resort safety path when the rest of the logger cannot be trusted. Only the descriptor-opening half remains here, because it needs `<filesystem>` and `Exception.h` includes the reporter.

### 9. Operation context

Defined in `OperationContext.h`.

This system adds request-level correlation through `OperationId` and `OperationScope`.

- `OperationId` is a globally unique identifier generated for a logical request
- `OperationScope` installs the current operation on the current thread while a synchronous block executes
- log records can include `operationId` metadata to help correlate related logs across asynchronous work

This is useful when a single business operation spans several threads or nested function calls.

## Logging lifecycle

The lifecycle is roughly:

1. `Logger::Initialize(...)` validates options and starts a writer thread.
2. `Logger::RegisterCategory(...)` assigns a stable category identity.
3. Log calls capture metadata, format arguments and message text.
4. Records are queued and the queue charges admission for memory usage.
5. The writer thread filters, renders, flushes and retains records.
6. `Logger::Flush()` forces pending sink writes.
7. `Logger::Shutdown()` stops the queue, drains remaining output and shuts down the writer.

## Filtering and routing

A record is admitted only if it passes the session's live rules and each destination's own threshold. In practice, the system uses:

- global minimum level
- category override level
- sink minimum level
- manual replay override (for history dumps)

This means the same log event can be retained for diagnostics even if it is not sent live to every sink.

## History and replay

The logger includes a bounded history mechanism that stores recent records for diagnostic inspection. This is separate from the live sink configuration and can be independently filtered by level.

When a write meets the history trigger threshold, the writer can replay relevant records to sinks or generate a history dump. That is useful when diagnosing failures without needing a new user-made log call.

## Summary

The LLUtils logging system is structured as a buffered, writer-owned pipeline:

- producers do lightweight capture
- the logger enforces session policy
- the writer filters and renders
- sinks receive text or metadata only as needed
- emergency subsystems stay available under failure conditions

That split keeps logging fast, deterministic and resilient even for high-volume or failure-heavy programs.
