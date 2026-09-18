# Exception diagnostics

[`PlatformUtility::GetCallStack`](../Include/LLUtils/PlatformUtility.h) captures stack addresses for diagnostic reporting.

## Capabilities

- Capture at most 64 frames and apply the requested skip count safely.
- Keep captured addresses when optional symbols are disabled, busy, or unavailable.
- Resolve Linux symbols in process without launching a shell.

## Reasoning

A failing thread should not wait indefinitely for another thread's symbol lookup. Windows uses a nonblocking symbol lock and retains raw addresses when enrichment cannot proceed.

## Complexity

Capture uses at most 64 frames. Allocation and platform symbol lookup add costs; the frame bound is not a latency guarantee.
