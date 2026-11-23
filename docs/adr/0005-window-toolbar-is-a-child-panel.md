# 5. The window toolbar (the pill) is a child panel

## Context

The pill must float on the page. A subview of the content view would be part of the page layout, and it would take mouse events from the engine view below it.

## Decision

`buildPill` makes the pill a borderless, non-activating `NSPanel` that is a child window of the browser window. The controls stay out of the layout and hit-test area of the page.

## Consequences

The pill must be attached, detached and positioned in code (`attachPill`, `positionPill`). It is removed during the Focus mode transition.
