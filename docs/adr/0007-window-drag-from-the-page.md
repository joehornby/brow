# 7. The window moves when the top edge of the page is dragged

## Context

The Brow window has no title bar. The page fills the window, thus there is no standard area to drag.

## Decision

- `shouldStartWindowDrag` accepts a mouse-down in a band 4 pt to 32 pt below the top of the page, and 5 pt in from the left and right sides. The outer edges stay with AppKit for resize. The palette card keeps its own events.
- The function looks at the subsequent event and does not remove it from the queue. Only a drag goes to the window server. A click on the header of a site still gets its initial down and up pair.
- Option-drag moves the window from all points on the page.

## Consequences

A drag that starts in the band does not go to the page.
