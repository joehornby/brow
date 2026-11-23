# 6. Brow menu commands get key events before the page

## Context

Chromium handles key equivalents in its renderer view. Without intervention, a page in a Chromium tab takes shortcuts such as ⌘L and ⌘W.

## Decision

`BrowApplication sendEvent:` offers a key-down event to the main menu first when the event is one of the Brow shortcuts. All other keys, which include text editing keys, go through AppKit as usual. A modal window turns this off.

## Consequences

The reserved key list in `sendEvent:` must agree with the menu that `applicationDidFinishLaunching:` builds. A new menu shortcut needs an entry there.
