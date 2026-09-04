# Changelog

#### Table of Contents
- [Unreleased](#unreleased)
- [2.0.3-ci303.7](#203-ci3037---2026-08-23)
- [2.0.3-ci303.6](#203-ci3036---2026-08-11)
- [2.0.3-ci303.5](#203-ci3035---2026-08-08)
- [2.0.3-ci303.2](#203-ci3032---2026-05-04)
- [2.0.2](#202---2024-08-11)
- [2.0.1](#201---2024-08-11)
- [2.0.0](#200---2024-08-11)
- [1.2.0](#120---2019-09-11)
- [1.1.0](#110---2018-11-07)
- [1.0.0](#100---2018-11-06)

___

## [Unreleased][]

### Changed
- Replaced external artwork uploader commands with native Catbox and Imgur uploads for local and embedded artwork.
- Converted local artwork uploads to WebP and added configurable maximum dimensions, defaulting to 250 x 250 pixels while preserving aspect ratio.

### Fixed
- Send local artwork bytes directly to native upload hosts, avoiding failures
  opening Unicode local paths through libcurl.

## [2.0.3-ci303.7][] - 2026-08-23

### Added
- Added a Providers preference tab for artwork source requirements, local or embedded artwork upload settings, and masked TheAudioDB credentials.
- Added TheAudioDB as an optional artwork fallback using a user-supplied supporter key stored in Windows Credential Manager.

### Changed
- Moved provider-specific artwork controls out of Main and Advanced without changing their persisted settings.
- Changed artwork resolution to try local or embedded artwork through the configured uploader, MusicBrainz / Cover Art Archive, and TheAudioDB in order, while keeping cache results and live status provider-specific.
- Made rejected TheAudioDB credentials suppress further automatic requests until the credential is replaced or explicitly tested again.

### Fixed
- Prevented Windows Credential Manager read failures from escaping the artwork worker and disrupting presence updates.
- Made clearing a stored TheAudioDB key follow the Preferences Apply/Reset lifecycle instead of deleting it before the settings change was applied.
- Preserved pending TheAudioDB key edits and test completion state when switching between preference tabs.
- Made custom-uploader tests respond to cancellation and clean up their child process.
- Made the local build helper discover and validate an available Python 3.10 or newer interpreter instead of relying solely on the Windows `py` launcher.

### Security
- Redacted TheAudioDB v1 keys from URL-path logging, kept provider credentials out of the artwork cache, and documented that secrets must not be placed in custom-uploader command lines.

## [2.0.3-ci303.6][] - 2026-08-11

### Added
- Added explicit album-artwork display policies: prefer artwork with a configured large-image fallback, always use that large image, or artwork only.
- Added live artwork status in Preferences for fetching, resolved, cached no-match, and failure states.
- Added a Troubleshooting tab explaining Discord recognised-application conflicts and the supported per-application fix.
- Added a Discord asset-key manifest and aligned the included playback-state PNG filenames with their Portal keys.

### Changed
- Made the existing artwork-first behaviour explicit while preserving it as the default.
- Changed the default Discord application to the maintained `Foobar2000` application; a persisted one-time migration replaces only the legacy default ID and preserves existing custom IDs.
- Renamed the Advanced preference label from application token to application ID and aligned playback asset filenames with their Portal keys.
- Introduced a version 4 artwork cache with provider-qualified keys and release-MBID identity; ambiguous older cache entries are rebuilt automatically.
- Made MusicBrainz pacing, retry waits, and active transfers respond promptly to component shutdown.

### Fixed
- Loaded the artwork cache before sending the initial Discord presence so cached album art is applied immediately after startup.
- Fixed MusicBrainz and uploader cache results suppressing or being attributed to the other provider.
- Fixed cache clearing or reloading racing with in-flight artwork work and allowing stale results or status to return.
- Fixed incomplete metadata, pending setting changes, and configured-large-image mode allowing stale artwork results to replace the current image or misreport live status.
- Fixed cache load/clear actions reporting success after failure and made successful operations re-evaluate the active track where applicable.
- Fixed Discord reconnect restoring a presence that should remain hidden while playback is paused.
- Kept Discord Rich Presence available with its configured fallback if the artwork worker cannot start or stops unexpectedly.

## [2.0.3-ci303.5][] - 2026-08-08

### Added
- Added current-track Rich Presence previews and an artwork-uploader test action.
- Added focused validation and release-metadata tests to CI.

### Changed
- Restored Discord presence automatically after Discord starts or reconnects.
- Added bounded MusicBrainz retries, request pacing, response-log truncation, and resilient per-release lookup.
- Reworked the artwork cache with positive and negative expiry, bounded size, atomic persistence, and legacy-cache migration.
- Made embedded-art temporary files unique and self-cleaning, restricted generated file extensions, and required secure uploader URLs.
- Removed non-functional duration controls and reduced sensitive uploader logging.
- Restricted GitHub release write permission to the release job and enforced tag/version consistency.
- Made submodule setup discover configured submodules from `.gitmodules` instead of generated directories.

### Fixed
- Fixed an artwork worker startup race that could read the worker thread handle before it existed.
- Fixed unsynchronised artwork cache reads and writes between the worker, playback updates, and Preferences UI.
- Fixed malformed MusicBrainz responses and invalid custom uploader output escaping the artwork worker.
- Fixed invalid playback duration values producing unsafe Discord timestamps.
- Fixed cache folder opening and uploader subprocess failures relying on release-build assertions, unchecked Shell API results, or unbounded output capture.
- Fixed shutdown and subprocess setup paths that could leave delayed Discord refresh callbacks or suspended uploader processes behind.
- Fixed oversized artwork cache files and null Discord ready callbacks being able to destabilise startup/logging paths.
- Fixed release-build artwork paths relying on assumptions about valid foobar album-art handles and generated cache keys.
- Fixed unbounded artwork cache keys from malformed tags or title-format queries.
- Fixed unexpected artwork worker exceptions being able to terminate the host process.
- Fixed delayed dynamic-info refresh callbacks running after component shutdown has started.
- Fixed remaining queued artwork refresh, cache persistence, and per-request artwork exception paths that could outlive shutdown or stop the artwork worker.
- Fixed malformed MusicBrainz album IDs being passed to GUID parsing without a fixed-length pre-check.
- Fixed setup and packaging helpers relying on optimisable Python assertions or shell command strings.
- Fixed dependency project templates defaulting only to the VS2022 toolset and missing the CMake compatibility flag needed by current CMake.

## [2.0.3-ci303.2][] - 2026-05-04

### Added
- Added a configuration guide with title formatting examples and album art setup notes.
- Added GitHub Actions builds for x64 and Win32 packages.
- Added an art-cache clear button.

### Changed
- Made build setup safer by requiring an explicit flag before resetting submodules.
- Improved local build compatibility with newer Visual Studio and installed Windows SDK versions.
- Hardened album art fetching, playback time parsing, and uploader subprocess handling.
- Improved MusicBrainz request metadata and artist fallback behavior.

## [2.0.2][] - 2024-08-11

### Fixed
- Fixed wrong art request data being used when processing fetched url (#65).

## [2.0.1][] - 2024-08-11

### Added
- Added debug logging (`Preferences`>`Advanced`>`Tools`>`Discord Rich Presence Integration`).

### Changed
- User MBID is now skipped if it's malformed.

### Fixed
- Fixed wrong album/artist values being used in MusicBrainz fetcher (#63).
- Fixed incorrect path being passed to uploader when using embedded art (#64).

## [2.0.0][] - 2024-08-11

### Added
- Added x64 support (#39).
- Added dark-mode support (#61).
- Added option to fetch and display album art from MusicBrainz (#6).
- Added option to upload and display art from foobar2000 (requires external tools, not included in component) (#62).
- Added text refresh when dynamic track info changes (no more than once in 30 seconds, due to Discord API limitations) (#50).

### Changed
- !!! Now requires foobar2000 v2.0+ !!!
- Changed Rich Presence activity type from `Playing a game` to `Listening to` (#2).
  This change has also other effects, due to the way it's implemented in Discord API:
	- Additional middle text field was added.
	- Playback time is no longer displayed.
	- Big image hover text is the same as middle text.

### Fixed
- Fixed inconsistent behaviour when pausing/stoping playback (#21).
- Fixed tabs not receiving focus on `tab` press in Preferences (#23).
- Fixed typo in component name in Preferences (#41).
- Fixed various corner cases when multibyte characters were used in text queries (#57)

## [1.2.0][] - 2019-09-11
### Added
- Added playback status images.
- Added new options to `main` Preferences tab:
  - Playback status image: light, dark, disabled.
  - Disable Rich Presence when playback is paused.
  - Swap `paused` and `playing` images.
- Added `advanced` Preferences tab with options to customize component:
  - Discord application key.
  - Resource IDs for corresponding images in the component.
- Added a link to the title formatting help in `main` Preferences tab.

### Changed
- Improved the frequency of presence updates.

### Fixed
- Fixed title formatting not updating when pausing and resuming playback.
- Fixed one-character text not displaying.

## [1.1.0][] - 2018-11-07
### Added
- Added Preferences page with the following settings:
  - Text fields configuration via title formatting queries.
  - Track duration: elapsed, remaining, disabled.
  - Foobar2000 image: light, dark, disabled.
- Added main menu command to toggle component.

### Fixed
- Fixed some bugs with persistent track info.

## [1.0.0][] - 2018-11-06
Initial release.

[unreleased]: https://github.com/Ci303/foo_discord_rich/compare/v2.0.3-ci303.7...HEAD
[2.0.3-ci303.7]: https://github.com/Ci303/foo_discord_rich/compare/v2.0.3-ci303.6...v2.0.3-ci303.7
[2.0.3-ci303.6]: https://github.com/Ci303/foo_discord_rich/compare/v2.0.3-ci303.5...v2.0.3-ci303.6
[2.0.3-ci303.5]: https://github.com/Ci303/foo_discord_rich/compare/v2.0.3-ci303.2...v2.0.3-ci303.5
[2.0.3-ci303.2]: https://github.com/Ci303/foo_discord_rich/releases/tag/v2.0.3-ci303.2
[2.0.2]: https://github.com/TheQwertiest/foo_discord_rich/compare/v2.0.1...v2.0.2
[2.0.1]: https://github.com/TheQwertiest/foo_discord_rich/compare/v2.0.0...v2.0.1
[2.0.0]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.2.0...v2.0.0
[1.2.0]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.1.0...v1.2.0
[1.1.0]: https://github.com/TheQwertiest/foo_discord_rich/compare/v1.0.0...v1.1.0
[1.0.0]: https://github.com/TheQwertiest/foo_discord_rich/commits/v1.0.0
