# 1. Focus mode backdrop colours use OKLCH

## Context

Focus mode draws a gradient around the page. The gradient comes from a sample of the visible page. A blend in sRGB between two different sites goes through grey, and a clipped colour changes its hue.

## Decision

- `native/FocusColor.h` does all backdrop colour work in OKLCH, the polar form of Oklab. The conversion matrices assume D65 linear sRGB.
- When a gradient stop is out of the sRGB gamut, `focusColor` decreases only the chroma. Hue and lightness stay the same.
- `siteGradient` gives more weight to chromatic regions of the sample. A coloured masthead then stays in the result on a page that is mostly white.
- On a page that is mostly white, the accent hue also goes into the darker end of the gradient. A neutral page keeps a neutral gradient.
- `setBackdropColors:duration:` makes keyframes in OKLCH. The hue follows the shorter arc during the full transition. This is also true when a new sample interrupts a transition between two very different sites.

## Consequences

Colour maths must stay in `FocusColor.h`. Do not blend backdrop colours in sRGB, and do not let Core Animation interpolate the two end states directly.
