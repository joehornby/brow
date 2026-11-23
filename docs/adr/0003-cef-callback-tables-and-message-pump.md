# 3. CEF callback tables are static, and CEF work runs from an AppKit run-loop timer

## Context

Brow uses the CEF C API from `native/chromium.m`. CEF objects are reference counted. Brow owns the AppKit run loop, thus CEF needs an external message pump.

## Decision

- The callback tables (`application`, `client`, `life`, `process`, `display`, `captureObserver`) are immutable singletons that live for the full process. Their reference-count functions do nothing (`permanent`).
- `schedule` runs `cef_do_message_loop_work` from an `NSTimer` on the main run loop in the common modes. It does not use a GCD main-queue block. Chromium does nested work that needs the main queue, and a block on that queue stops it.

## Consequences

Do not allocate these tables on the heap, and do not move the pump to `dispatch_async(dispatch_get_main_queue(), …)`.
