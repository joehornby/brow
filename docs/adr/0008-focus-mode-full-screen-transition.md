# 8. Focus mode lets Spaces do the full-screen transition

## Context

The default full-screen animation scales a snapshot of the window. A snapshot that contains engine layers shows artefacts when the compositor scales it.

## Decision

In Focus mode, the window delegate supplies the custom full-screen animation callbacks. They set the final frame one time, lay out the page at its final size, and draw. Spaces does the desktop transition.

## Consequences

The window does not animate its size during the Focus mode transition. Regular full screen (⌃⌘F) keeps the system animation.
