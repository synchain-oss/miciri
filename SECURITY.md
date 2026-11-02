# Security Policy

## Supported versions

Miciri has no release yet. Once releases exist, only the latest minor line (`x.Y.*`) receives security fixes.

## Reporting a vulnerability

**Do not open a public issue.** Use GitHub Private Vulnerability Reporting: repository page → Security → Report a vulnerability.

If that channel is unavailable, email **contact@synchain.ca**.

Please include the Miciri version, your DAW and its version, your OS, steps to reproduce, and if possible the plugin state bytes that trigger the problem. **Do not include any credentials or audio recordings.**

## Response targets

- First response: within 3 business days
- Fix or mitigation: 14 days for high severity, 30 days for medium
- Disclosure: a public advisory (GHSA) 7 days after the fix is released

## Scope

- In scope: memory-safety bugs in this repository's code; crashes, arbitrary writes or resource exhaustion from **plugin state deserialization**; payloads from the editor's web layer that crash or corrupt the C++ side; supply-chain issues in build scripts and CI.
- Out of scope: upstream issues in JUCE, the VST3 SDK, the Audio Unit SDK or the WebView runtimes (please report those upstream); feedback or loud output caused by the user's own monitoring setup; scenarios that already require arbitrary code execution on the user's machine.
