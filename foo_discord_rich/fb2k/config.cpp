#include <stdafx.h>

#include "config.h"

#include <discord/discord_integration.h>
#include <utils/discord_application_id_migration.h>

namespace drp::config
{

namespace
{

qwr::fb2k::ConfigBool discordApplicationIdMigrationComplete( guid::conf_discord_application_id_migration_complete, false );

}

qwr::fb2k::ConfigBool isEnabled( guid::conf_is_enabled, true );
qwr::fb2k::ConfigUint8Enum<ImageSetting> largeImageSettings( guid::conf_large_image_settings, ImageSetting::Light );
qwr::fb2k::ConfigUint8Enum<ImageSetting> smallImageSettings( guid::conf_small_image_settings, ImageSetting::Light );
qwr::fb2k::ConfigUint8Enum<TimeSetting> timeSettings( guid::conf_time_settings, TimeSetting::Disabled );
qwr::fb2k::ConfigBool enableAlbumArtFetch( guid::conf_enable_album_art_fetch, true );
qwr::fb2k::ConfigBool enableCatboxUpload( guid::conf_enable_catbox_upload, false );
qwr::fb2k::ConfigBool enableImgurUpload( guid::conf_enable_imgur_upload, false );
qwr::fb2k::ConfigBool enableTheAudioDbFetch( guid::conf_enable_theaudiodb_fetch, false );
qwr::fb2k::ConfigUint8Enum<artwork::DisplayPolicy> artworkDisplayPolicy( guid::conf_artwork_display_policy, artwork::DisplayPolicy::PreferArtwork );
qwr::fb2k::ConfigString imgurClientId( guid::conf_imgur_client_id, "" );
qwr::fb2k::ConfigString localArtworkPinQuery( guid::conf_art_upload_pin_query, "%artist%|%album%" );
qwr::fb2k::ConfigUint32 localArtworkUploadMaxWidth( guid::conf_local_artwork_upload_max_width, 250 );
qwr::fb2k::ConfigUint32 localArtworkUploadMaxHeight( guid::conf_local_artwork_upload_max_height, 250 );

qwr::fb2k::ConfigString topTextQuery( guid::conf_top_text_query, "[%title%]" );
qwr::fb2k::ConfigString middleTextQuery( guid::conf_middle_text_query, "[by %album artist%]" );
qwr::fb2k::ConfigString bottomTextQuery( guid::conf_bottom_text_query_v2, "[on %album%]" );
qwr::fb2k::ConfigString bottomTextQuery_v1_deprecated( guid::conf_bottom_text_query_v1_deprecated, "[%album artist%[: %album%]]" );

qwr::fb2k::ConfigString discordAppToken( guid::conf_app_token, kDefaultDiscordApplicationId );
qwr::fb2k::ConfigString largeImageId_Light( guid::conf_large_image_id_light, "foobar2000" );
qwr::fb2k::ConfigString largeImageId_Dark( guid::conf_large_image_id_dark, "foobar2000-dark" );
qwr::fb2k::ConfigString playingImageId_Light( guid::conf_playing_image_id_light, "playing" );
qwr::fb2k::ConfigString playingImageId_Dark( guid::conf_playing_image_id_dark, "playing-dark" );
qwr::fb2k::ConfigString pausedImageId_Light( guid::conf_paused_image_id_light, "paused" );
qwr::fb2k::ConfigString pausedImageId_Dark( guid::conf_paused_image_id_dark, "paused-dark" );

qwr::fb2k::ConfigBool disableWhenPaused( guid::conf_disable_when_paused, false );
qwr::fb2k::ConfigBool swapSmallImages( guid::conf_swap_small_images, false );

void MigrateLegacyDiscordApplicationId()
{
    const auto migrationComplete = static_cast<bool>( discordApplicationIdMigrationComplete );
    const auto applicationId = static_cast<std::string>( discordAppToken );
    if ( ShouldMigrateLegacyDiscordApplicationId( migrationComplete, applicationId ) )
    {
        discordAppToken = kDefaultDiscordApplicationId;
        LogDebug( "Migrated the legacy Discord application ID to the maintained Foobar2000 application" );
    }

    if ( !migrationComplete )
    {
        discordApplicationIdMigrationComplete = true;
    }
}

void SanitiseArtworkDisplayPolicy()
{
    const auto policy = artworkDisplayPolicy.GetValue();
    const auto normalisedPolicy = artwork::NormaliseDisplayPolicy( policy );
    if ( policy != normalisedPolicy )
    {
        LogWarning( "Unknown artwork display behaviour was reset to the default" );
        artworkDisplayPolicy = normalisedPolicy;
    }
}

} // namespace drp::config
