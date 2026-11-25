# 12. Each engine scales its viewport in a different way

## Context

A viewport shows the page at a fixed layout size, such as 402 × 874 points, in a tile that is smaller or larger than that size. The content must scale with the tile, and a click must go to the correct point on the page.

WebKit takes its layout size from the frame of its view. Chromium takes its layout size from the rectangle of its view in window coordinates, so a scaled parent view changes the layout width.

## Decision

- Each page is in a `Tile` view in its `Surface`. `arrange` sets the frame of each tile.
- For WebKit, the bounds of the tile are the viewport size. AppKit scales the tile, and it converts mouse events.
- For Chromium, the tile is not scaled. `emulateViewport:` sends `Emulation.setDeviceMetricsOverride` with the viewport size and the scale.
- While viewports show, the content view of the window has no background and no corner radius. The window is an invisible frame, and the system draws a shadow for each tile.
- `arrangeViewports` tries each division of the viewports into rows, in their order. It keeps the division that covers the largest area. All rows have the same width and the same gap.
- The order of the viewports is the order of the tiles in the surface. A drag of a caption moves its tile with the pointer. A drop moves the viewport in the list of selected viewports, and `syncViewports` sorts the tiles into that order, so each page keeps its viewport size.
- The drop animation is a spring on the transform of each tile layer, not on the engine view. AppKit keeps the scale of a WebKit tile in the transform of its layer, so `shown` adds the movement to that transform and does not replace it.

- When the page in one tile goes to a new address by itself, `followNavigation` sends the other tiles to that address. A tile that Brow navigated does not lead until its load is complete, so two tiles with different redirects do not send each other in a circle.

## Consequences

`emulateViewport:` tells the surface which method a page uses. A new engine must select one of the two methods. The corner radius of a WebKit tile is in viewport units, so `arrange` divides it by the scale.
