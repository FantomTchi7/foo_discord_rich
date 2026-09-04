# Configuration Guide

Discord Rich Presence Integration uses foobar2000 title formatting strings for
the text shown in Discord. You can edit these in:

`Preferences > Tools > Discord Rich Presence Integration > Main`

## Text Fields

The component exposes three text fields:

- Top: usually the track title or main display line.
- Middle: usually the artist.
- Bottom: usually the album, release, playlist, or playback context.

Use **Preview current track** to evaluate the unsaved Top, Middle, and Bottom
fields against the currently playing track before applying the settings.

Discord's `Listening to ...` application label is controlled by Discord and
cannot be changed per track. The title-formatting fields below it are the parts
this component can update dynamically.

## Recommended Layouts

### Track, Artist, Album

Top:

```text
[$if2(%title%,%filename%)]
```

Middle:

```text
[$if2(%artist%,Unknown artist)]
```

Bottom:

```text
[$if2(%album%,Unknown album)]
```

### Artist - Track

Top:

```text
[$if2(%artist%,Unknown artist) - $if2(%title%,%filename%)]
```

Middle:

```text
[$if2(%album%,Unknown album)]
```

Bottom:

```text
[]
```

### Album-Focused

Top:

```text
[$if2(%album%,Unknown album)]
```

Middle:

```text
[$if2(%artist%,Unknown artist)]
```

Bottom:

```text
[$if2(%title%,%filename%)]
```

### Radio or Stream

Top:

```text
[$if2(%title%,$if2(%streamtitle%,%filename%))]
```

Middle:

```text
[$if2(%artist%,$if2(%album artist%,Live stream))]
```

Bottom:

```text
[$if2(%album%,$if2(%codec%,))]
```

## Common Variables

These are standard foobar2000 title-formatting fields commonly useful in
Discord presence:

| Variable | Meaning |
| --- | --- |
| `%title%` | Track title |
| `%artist%` | Track artist |
| `%album artist%` | Album artist |
| `%album%` | Album title |
| `%date%` | Release date or year |
| `%genre%` | Genre |
| `%tracknumber%` | Track number |
| `%discnumber%` | Disc number |
| `%filename%` | File name without extension |
| `%codec%` | Audio codec |
| `%bitrate%` | Bitrate |
| `%samplerate%` | Sample rate |
| `%length%` | Formatted track length |
| `%length_seconds%` | Track length in seconds |
| `%playback_time%` | Current playback time |
| `%playback_time_seconds%` | Current playback time in seconds |

Foobar2000 supports many more fields and functions. Use foobar2000's built-in
title formatting help for the complete reference.

## Useful Functions

### Fallbacks

Use `$if2(a,b)` when a tag may be missing:

```text
[$if2(%title%,%filename%)]
```

### Conditional Text

Square brackets display their contents only when all fields inside are present:

```text
[%artist% - %title%]
```

If either artist or title is missing, the whole line is hidden.

### Multiple Fallbacks

Use `$if3(a,b,c)` for several possible values:

```text
[$if3(%album artist%,%artist%,Unknown artist)]
```

### Lowercase

```text
[$lower(%artist%)]
```

### Text Cleanup

For short, clean Discord fields, avoid long technical strings unless you
specifically want them visible. Discord also applies length limits, so very long
results are truncated by the component.

## Album Art

The **Providers** tab controls artwork sources. Enabled providers are tried in
this fixed order:

1. Local or embedded front-cover artwork uploaded to Catbox.
2. Local or embedded front-cover artwork uploaded to Imgur.
3. MusicBrainz and the Cover Art Archive.
4. TheAudioDB.

A clean no-match result advances to the next provider. A temporary provider
failure also permits a fallback and is held in memory briefly to avoid a tight
retry loop. Successful results and clean no-match results use separate,
provider-qualified cache entries.

The Main tab also provides an **Artwork behaviour** selector:

- **Prefer artwork; use large-image fallback** requests artwork first and
  displays the configured large image while artwork is being fetched or when no
  match exists. This is the default and preserves previous behaviour.
- **Use configured large image only** skips artwork requests
  and uses the configured large image. If that image is disabled, no large
  image is shown.
- **Album artwork only; no fallback image** requests artwork but leaves the
  large image empty while fetching or when no match exists.

The artwork status line reports whether the applied configuration is idle,
fetching, resolved, using a cached no-match result, or encountered a
fetch/upload failure. Unsaved artwork changes are labelled as pending rather
than being mixed with live status. Resolved status identifies the provider;
URLs and credentials are not shown.

### MusicBrainz / Cover Art Archive

This provider requires no account or API key. Enable it on the **Providers**
tab and make sure tracks have useful `%artist%` and `%album%` tags. Prefer
MusicBrainz album ID tags where available:

- `MUSICBRAINZ_ALBUMID`
- `MUSICBRAINZ ALBUM ID`

A valid release ID identifies the release directly. Otherwise the artist and
album identify the lookup. MusicBrainz requests are paced to comply with its
public API requirements.

### TheAudioDB

TheAudioDB is an optional automatic fallback on the **Providers** tab. It uses
artist and album metadata and accepts only an exact normalised album match.

- This distributed component requires your own supporter key. TheAudioDB's
  current [terms](https://www.theaudiodb.com/docs_terms_of_use.php) reserve its
  shared free key for development projects and do not permit publishing
  free-key apps to an app store. The component therefore rejects the shared
  development key `123`.
- Obtain a key from TheAudioDB's
  [API-key page](https://www.theaudiodb.com/api_apply.php), then paste it into
  the masked field. The [official API documentation](https://www.theaudiodb.com/free_music_api)
  explains the access levels and rate limits.
- The key is masked in Preferences, removed from request logs, and stored in
  Windows Credential Manager for the current Windows user. The saved value is
  never redisplayed in Preferences.

The component never writes the key into its artwork cache or diagnostic web
request URL. Use **Clear stored key** to remove it; TheAudioDB remains
unchanged until you select **Apply**; cancelling or resetting Preferences keeps
the stored credential. After it is removed, TheAudioDB remains unavailable
until a replacement is saved. After an HTTP 429 response, TheAudioDB is
skipped for all tracks using that key for one minute; it becomes eligible again
on the next presence refresh after that cooldown.

### Local and embedded artwork

foobar2000 can provide a local `folder.jpg`-style front cover or artwork
embedded in the current audio file. Discord cannot read a path on this computer
and cannot receive those image bytes directly through Rich Presence, so the
component needs a public HTTPS image URL.

- **Catbox** supports anonymous uploads and needs no configuration.
- **Imgur** uses anonymous image uploads but requires a Client ID from an Imgur
  application you register. A Client ID identifies the application and is not
  an account access token; do not enter a client secret, password, or OAuth
  token in this field.

Enable either host on the **Providers** tab and use its **Test** action against
the current track before applying the setting. The shared cache pin query
identifies when an upload can be reused; its default is `%artist%|%album%`.
Artwork bytes are sent directly from foobar2000 to the selected host, avoiding
temporary files and third-party uploader processes.
Before upload, artwork is converted to WebP and proportionally downsized to fit
the configured **Maximum size**. The default is **250 x 250 pixels**; smaller
artwork is not enlarged, and rectangular covers retain their aspect ratio.
Uploads remain cancellable and allow up to two minutes for a host to receive a
large artwork file after the connection has been established.

### Provider requirements and exclusions

Discord does not require an API key, bot token or client secret for this
component. Rich Presence uses the public **Application ID** shown on the
Advanced tab. If you deliberately create a custom application in the
[Discord Developer Portal](https://discord.com/developers/applications), copy
**General Information > Application ID** only.

Discogs is not offered as an artwork source. Its current
[API terms](https://support.discogs.com/hc/en-us/articles/360009334593-API-Terms-of-Use)
treat images as restricted data, constrain caching, and require linked "Data
provided by Discogs" attribution beside each use. A Discord activity image
cannot reliably satisfy those requirements. A personal Discogs token does not
remove them.

TheAudioDB is credited in the provider name and this guide, as required for
paid API use. Artwork remains subject to the rights of its respective owner.

### Artwork cache

Artwork is cached separately for Catbox, Imgur, MusicBrainz, and TheAudioDB so
one provider's result cannot suppress another. The WebP maximum-size profile is
also part of each local-upload cache key, so changing it does not reuse a URL
for a differently sized image. Successful results are refreshed
periodically, while clean no-match results expire sooner. Cache formats older
than version 6 are deliberately ignored because their provider, upload profile,
and release
identity is ambiguous; the component rebuilds them as tracks are played.

Use **Advanced > Artwork cache > Reload from disk** to reload the current cache file, or use
**Clear cache** if a bad lookup is cached. Load reports when no file exists.
After a successful load or clear, the component immediately re-evaluates the
current track where artwork is enabled; file errors are reported instead of
being shown as successful operations.

## Recommended Settings

### Simple Track / Artist / Album

- Top: `[$if2(%title%,%filename%)]`
- Middle: `[$if2(%artist%,Unknown artist)]`
- Bottom: `[$if2(%album%,Unknown album)]`
- Enable MusicBrainz / Cover Art Archive on the Providers tab.
- Optionally enable TheAudioDB as a fallback.
- Enable Catbox for zero-configuration local artwork uploads, or configure an
  Imgur Client ID before enabling Imgur.

### Album Art First Run

Artwork lookup runs in the background. The first play may briefly show the
large-image fallback, then update after a provider returns artwork. Subsequent
plays use the local artwork cache.

## Playback Images

The component can show small playback status images:

- Playing image
- Paused image
- Disabled

If `Disable Rich Presence when paused` is enabled, Discord presence is cleared
while playback is paused instead of showing the paused state.

## Discord Application and Assets

This fork defaults to the maintained Discord application `Foobar2000`, with
application ID `1536157545863847938`. On the first startup after upgrading,
the component replaces the legacy default ID (`507982587416018945`) and records
that the migration has run. Any other custom application ID is preserved, and
the legacy ID can still be selected deliberately afterwards while remaining on
this or a newer release. Downgrading can discard the migration marker, so a
later re-upgrade may migrate the legacy ID again.

The six configured Rich Presence asset keys are:

- `foobar2000`
- `foobar2000-dark`
- `playing`
- `playing-dark`
- `paused`
- `paused-dark`

The key mapping, included playback-state PNGs, and Developer Portal metadata
guidance are recorded in the [Discord asset manifest](../images/README.md).
Portal artwork is not read locally or bundled into the `.fb2k-component`;
Discord serves uploaded assets by key.

## Troubleshooting

### Discord shows another application instead of foobar2000

Discord's automatic recognition and this component's Rich Presence are separate
activities. When both are active, Discord may show a recognised game or
application instead of the foobar2000 activity, particularly in the compact
profile card. This component cannot inspect, reorder, or suppress other Discord
activities.

To prefer foobar2000 over a conflicting recognised application:

1. Open `Discord > User Settings > Registered Games`.
2. Find the conflicting application and turn off its eye icon.
3. Under `Activity Sharing`, keep `Share my activity` enabled so this
   component's Rich Presence can still be displayed.

The artwork behaviour setting changes only the large image on the foobar2000
activity card. It cannot replace another application's icon or control which
activity Discord chooses to display.

### Album art does not appear

- Check the artwork status shown on the Main tab.
- Select `Prefer artwork` or `Album artwork only`; `Use configured large image
  only` intentionally skips artwork requests.
- Confirm Catbox or Imgur is enabled for local artwork uploads. Imgur also
  requires a Client ID.
- Confirm the track has artist and album tags.
- Try a release with a MusicBrainz album ID tag.
- Use `Advanced > Artwork cache > Open folder...` to inspect cached
  results.
- Enable debug logging in foobar2000 Advanced Preferences if you need request
  details.

### Text is blank

Square brackets hide their contents when fields are missing. Use `$if2` if you
want fallback text:

```text
[$if2(%title%,%filename%)]
```

### `Listening to foobar2000` does not change

That text is Discord's application name. Discord does not allow this component
to change it per track. Put dynamic track details in Top, Middle, and Bottom.
