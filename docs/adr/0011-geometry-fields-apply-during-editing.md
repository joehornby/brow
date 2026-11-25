# 11. Geometry fields apply during editing and ignore values that do not fit

## Context

The width and height fields change the window frame while the person types. A value such as `1` on the way to `1200` is a valid editing state, but it is smaller than the minimum window size.

## Decision

`applyFields:centre:` skips a width or height that is smaller than the minimum. The frame stays stable until the dimension fits. The text that the person typed is not replaced.

A number that is larger than the screen cannot become valid when the person types more digits. `applyFields:centre:` applies the screen limit and replaces the text with that limit.

## Consequences

The field can show a value that is smaller than the window frame during editing. `syncGeometryFields` corrects the field when editing ends.
