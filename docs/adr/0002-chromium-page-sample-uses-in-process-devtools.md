# 2. The Chromium page sample uses an in-process DevTools message

## Context

`samplePage:` gives a small image of the visible page for the Focus mode backdrop. CEF in windowed mode has no direct snapshot call. A remote debugging port would give one, but an open port is a security risk.

## Decision

- `ChromiumPage` sends `Page.captureScreenshot` through `send_dev_tools_message` and reads the reply through a DevTools message observer. No debugging port is opened.
- The observer accepts only the result of the request that Brow made (`captureID`).
- The capture is the visible viewport only (`captureBeyondViewport: NO`). This keeps the true scroll position. The browser UI makes the image smaller afterwards.
- The format is JPEG at quality 30. This puts a limit on the transfer cost for pages with many images.

## Consequences

Do not enable `remote-debugging-port` to get page images. A reply larger than 2 MB is ignored and the backdrop does not change.
