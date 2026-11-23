# 4. Quit completes the current event, then releases the engine views, then terminates

## Context

A quit request can come from the menu, a key, a window close, or the system. CEF sends its close notifications asynchronously and needs the usual AppKit run loop to continue. CEF also monitors the destruction of its `NSView`, and can ask to close the top-level window that all tabs share.

## Decision

- `prepareQuit` hides the windows, then defers the release of the pages to a run-loop block. The event that started the quit is complete before an engine view goes away.
- `BrowApplication terminate:` and `applicationShouldTerminate:` both call `prepareQuit`, because system termination requests can go directly to the delegate. The delegate returns `NSTerminateLater` and replies when Chromium has shut down.
- `ChromiumPage dispose` removes the CEF child view from the page before it calls `close_browser`. The engine then cannot close the shared window.

## Consequences

Do not release pages synchronously in a terminate path. Keep the two entry points.
