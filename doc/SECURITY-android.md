# WIP: VLC for Android-specific security information

This draft belongs in the `vlc-android` repository as `doc/SECURITY-specific.md`.

VLC for Android follows the [VideoLAN security policy](SECURITY.md).

## Reporting links

- **Public issue:** [open a public VLC for Android issue](https://code.videolan.org/videolan/vlc-android/-/issues/new).
- **Private report:** [open a confidential VLC for Android issue](https://code.videolan.org/videolan/vlc-android/-/issues/new?issue%5Bconfidential%5D=true).

## Scope and released builds

### Scope

| Area | Policy scope |
|---|---|
| **VLC for Android application** | This page covers the official application, its packaging, and bundled code. |
| **VLC core and Android modules** | VLC fixes land in `vlc`. |

### Release targets

Supported VLC for Android releases are versioned releases published by VideoLAN on the
[official Android download page](https://www.videolan.org/vlc/download-android.html) or through
stores linked from that page. The source is the matching
[`vlc-android` tag](https://code.videolan.org/videolan/vlc-android/-/tags).

For policy timelines, the direct download and each store release published by VideoLAN are separate
release targets.

Nightlies and development snapshots are development-only.

## Project-specific examples

These examples illustrate the assessment; they do not determine it by themselves.

| Channel | Examples |
|---|---|
| **Public: Non-security bug** | Ordinary crashes; memory leaks, including those causing out-of-memory conditions. |
| **Private: Low-risk security issue** | A reachable memory-safety fault without proof of attacker control; demonstrated attacker control with only a minor consequence. |
| **Private: High-risk security issue** | Bypassing an access control to obtain unauthorized access to user data or privileged functionality; causing VLC to access or expose protected files or network resources on behalf of an untrusted party. |
