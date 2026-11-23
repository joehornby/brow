# 10. Each page is in its own surface layer

## Context

Tab transitions animate opacity and transform. WebKit and CEF own the layers of their views, and an animation on those layers conflicts with the engine.

## Decision

- `host_create` puts each page in a `Surface` view that has its own compositing layer. All transition animations go on the surface layer.
- `depth` calculates the scale transform from the real `anchorPoint` of the layer, because a layer that AppKit backs can have its anchor at the origin.

## Consequences

Do not animate the engine view. Do not assume an anchor point of (0.5, 0.5).
