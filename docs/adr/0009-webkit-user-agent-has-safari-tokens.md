# 9. The WebKit user agent has the Safari product tokens

## Context

The default `WKWebView` user agent has the platform and engine tokens but no `Version/… Safari/…` tokens. Some sites, for example Google, serve legacy pages when these tokens are absent.

## Decision

`host_create` keeps the platform and engine tokens from WebKit and sets `applicationNameForUserAgent` to `Version/<installed Safari version> Safari/605.1.15`.

## Consequences

Sites see a WebKit tab as desktop Safari. If Safari is not installed, the default user agent is used.
