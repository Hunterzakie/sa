# VLC-specific security information

VLC follows the [VideoLAN security policy](SECURITY.md).

## Reporting links

- **Public issue:** [open a public VLC issue](https://code.videolan.org/videolan/vlc/-/issues/new).
- **Private report:** [open a confidential VLC issue](https://code.videolan.org/videolan/vlc/-/issues/new?issue%5Bconfidential%5D=true).

## Scope and released builds

### Scope

| Area | Policy scope |
|---|---|
| **VLC core and modules** | This repository owns fixes for VLC core and its shared or platform-specific modules. |
| **Bundled third-party libraries** | Report vulnerabilities to the upstream project. VideoLAN updates affected VLC releases when fixes become available. |
| **Downstream packages and products** | Organizations that package or embed VLC or libVLC are responsible for the security of their products. |
| **Linux distribution packages** | A Linux distribution's VLC packaging, patches, and dependencies are its own responsibility. Report distribution-specific security issues through the distribution's security channel. |
| **VLC for Android** | See [VLC for Android-specific information](https://code.videolan.org/videolan/vlc-android/-/blob/master/doc/SECURITY-specific.md). |
| **VLC for iOS** | See [VLC for iOS-specific information](https://code.videolan.org/videolan/vlc-ios/-/blob/master/doc/SECURITY-specific.md). |

### Release targets

| Target | Supported release |
|---|---|
| **Upstream source** | The upstream release branches corresponding to the Windows and macOS releases below. |
| **Windows build** | The release shown as current on the official [Windows download page](https://images.videolan.org/vlc/download-windows.html). |
| **macOS build** | The release shown as current on the official [macOS download page](https://images.videolan.org/vlc/download-macosx.html). |
| **Stable Snap** | The stable VLC Snap distributed and updated by VideoLAN through the [official Ubuntu download page](https://images.videolan.org/vlc/download-ubuntu.html). |

Versions older than the supported releases above are unsupported. Nightlies, snapshots, and code
that has not appeared in a supported release are development-only.

## Project-specific examples

These examples illustrate the assessment; they do not determine it by themselves.

| Channel | Examples |
|---|---|
| **Public: Non-security bug** | Ordinary crashes; memory leaks, including those causing out-of-memory conditions. |
| **Private: Low-risk security issue** | A reachable memory-safety fault without proof of attacker control; demonstrated attacker control with only a minor consequence. |
| **Private: High-risk security issue** | Code execution through attacker-controlled input; leakage of user data that an attacker can read; bypass of a security boundary. |
