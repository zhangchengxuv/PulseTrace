# PulseTrace

A local keyboard, mouse, application usage and network activity monitor for Windows.

[简体中文](README.md) · **English**

## Features

- **Keyboard**: aggregate key statistics, ANSI 104 layout and the continuous Arctic heatmap.
- **Mouse**: a five-button diagram and ranking; Mouse4 Back, Mouse5 Forward; temporal double clicks, wheel events and deltas tracked separately.
- **App Usage**: foreground dwell and active time, rankings, hourly/daily trends, exclusions and CSV export.
- **Network**: egress upload/download, physical-interface IPv4/IPv6 observations, monthly daily-traffic calendars, interfaces and connection history.
- **Campus portal**: optional generic portal monitoring with isolated WebView2 sign-in, DOM field recognition/confirmation and balance/allowance snapshots. Disabled by default.
- **Simpler pages**: Network no longer displays application traffic rankings or queries them on refresh; existing history is retained. Keyboard rankings collapse by default and preserve expansion state.
- **Cloud backup**: eight settings categories, Jianguoyun HTTPS WebDAV, DPAPI-protected credentials, encrypted archives and confirmed restore. [Backup guide and validation scope](docs/cloud-backup.md).
- **Taskbar speed**: a transparent native two-line widget with font, size, units, precision, color and position settings; floating/tray fallback.
- **History and settings**: All Data by default, minute/hour/day and custom ranges, SQLite, monthly archives, backups and export; Chinese/English, Deep Ocean / Pearl Mist, tray operation and dual-source update checks.

Collection starts paused. Input text, window titles and mouse trajectories are not recorded. Active time uses an idle threshold; IPv4/IPv6 and rankings depend on permissions, sampling and quality, and do not represent full-system traffic or campus billing. Campus monitoring is disabled by default, and unconfirmed portal fields remain unknown. Unsigned update packages require manual installation after verification. [Measurement limits](docs/traffic-rankings.md) · [App Usage](docs/app-usage.md) · [Campus portal](docs/campus-portal.md)

## Screenshots

Real x64 Release development build of main `b3a2cbf`, using isolated empty data and paused collection; no synthetic statistics or personal history. Pages use 1600×900 DIP; the taskbar image is an actual crop.

| Dashboard | Network |
| --- | --- |
| ![Dashboard](docs/screenshots/dashboard.png) | ![Network](docs/screenshots/network.png) |
| **ANSI 104 keyboard heatmap** | **Taskbar widget (paused)** |
| ![Keyboard heatmap](docs/screenshots/keyboard.png) | ![Taskbar widget](docs/screenshots/taskbar.png) |

<details>
<summary>Mouse and App Usage</summary>

| Mouse | App Usage |
| --- | --- |
| ![Mouse](docs/screenshots/mouse.png) | ![App Usage](docs/screenshots/usage.png) |

</details>

## Resource Usage

Measured main `d414ecf`: Windows 11 26300, Ryzen 5 5600 (12 logical processors), 31.92 GiB RAM, 125% DPI, WebView2 156.0.4314.58. Each run lasted 45 seconds; means use seconds 10–40, with monitoring enabled, no deliberate input/network load and ranking sources disabled.

| State | CPU (machine normalized) | Private Bytes | Working Set |
| --- | ---: | ---: | ---: |
| Background monitoring, tray only | 0.039% | 8.54 MiB | 34.69 MiB |
| Main window open, including WebView2 children | 0.111% | 262.10 MiB | 460.99 MiB |
| Background monitoring + taskbar speed | 0.022% | 9.20 MiB | 40.37 MiB |

All rows include the native helper; the taskbar helper alone used **2.81 MiB Private Bytes / 17.55 MiB Working Set**. Private Bytes measures private committed memory; Working Set measures resident pages, and summing processes double-counts shared pages. These short runs establish neither long-term limits nor performance improvements.

Current program files listed by the installer total **11.07 MiB**, excluding the uninstaller, system Runtime and `data`; the current installed size including the uninstaller remains untested. After 45 seconds, data files totaled approximately **0.12 MiB** for background/taskbar and **10.73 MiB** with the window open, including WebView2 cache. Measurement reports are excluded; file sizes are not disk writes. [Methods and results](docs/performance.md)

## Download and Install

[GitHub v0.7.0](https://github.com/zhangchengxuv/PulseTrace/releases/tag/v0.7.0) · [Gitee v0.7.0](https://gitee.com/zhangchenxuv/pulse-trace/releases/tag/v0.7.0)

Windows 10/11 x64 with Microsoft Edge WebView2 Evergreen Runtime is required.

- **Installer**: run `PulseTrace-Setup-*.exe`; data lives in the installation's `data` folder and is retained by default on uninstall.
- **Portable**: extract the complete ZIP, retain Web assets, DLLs and helper, then run `PulseTrace.exe`.

Version **v0.7.0** adds encrypted WebDAV backup, eight settings categories, campus overview cards and compact taskbar rates; simplifies Network/Keyboard views and includes all campus-picker fixes. Existing data and settings remain compatible. Live Jianguoyun and long-running validation limits are listed in the [release notes](docs/releases/v0.7.0.md). [User guide](docs/desktop-guide.md) · [Build from source](docs/build.md)

## Stack

C++20 · Win32 / Raw Input · WebView2 · SQLite · CMake

## AI Development and License

PulseTrace is an open-source project developed with the assistance of OpenAI Codex. Its code, interface, and documentation were created with AI assistance and validated through builds and testing. This is not an official OpenAI product.

- **AI tool**: OpenAI Codex.
- **Verified model**: GPT-6; unconfirmed historical models are omitted.
- **License**: project-owned code and documentation use the [MIT License](LICENSE). Dependencies retain their own licenses; see [third-party notices](THIRD-PARTY-NOTICES.md). TrafficMonitor was consulted for concepts; its code was not ported.
