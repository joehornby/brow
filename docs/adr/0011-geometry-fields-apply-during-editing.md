# 11. Geometry fields apply during editing and ignore values that do not fit

## Context

The width and height fields change the window frame while the person types. A value such as `1` on the way to `1200` is a valid editing state, but it is smaller than the minimum window size.

## Decision

`applyFields:centre:` skips a width or height that is out of range. The frame stays stable until the dimension fits. The text that the person typed is not replaced.

## Consequences

The field can show a value that is different from the window frame during editing. `syncGeometryFields` corrects the field when editing ends.
