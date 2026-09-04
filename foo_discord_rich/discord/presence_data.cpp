#include <stdafx.h>

#include "presence_data.h"

#include <artwork/fetcher.h>
#include <discord/discord_integration.h>
#include <fb2k/config.h>
#include <utils/validation.h>
#include <utils/artwork_policy.h>
#include <utils/credential_store.h>

#include <qwr/algorithm.h>

#include <atomic>
#include <cmath>
#include <limits>
#include <mutex>

namespace
{
namespace config = drp::config;

qwr::u8string EvaluateQueryForPlayingTrack( const metadb_handle_ptr& handle, const qwr::u8string& query )
{
    static std::unordered_map<qwr::u8string, titleformat_object::ptr> queryToTitleFormat;
    static std::mutex queryToTitleFormatMutex;

    auto pc = playback_control::get();

    titleformat_object::ptr tf;
    {
        std::scoped_lock lock{ queryToTitleFormatMutex };
        tf = qwr::FindOrDefault( queryToTitleFormat, query, titleformat_object::ptr{} );
        if ( tf.is_empty() )
        {
            titleformat_compiler::get()->compile_safe( tf, query.c_str() );
            queryToTitleFormat.try_emplace( query, tf );
        }
    }

    if ( tf.is_empty() )
    {
        return {};
    }

    pfc::string8_fast result;
    if ( pc->is_playing() )
    {
        metadb_handle_ptr dummyHandle;
        pc->playback_format_title_ex( dummyHandle, nullptr, result, tf, nullptr, playback_control::display_level_all );
    }
    else if ( handle.is_valid() )
    {
        handle->format_title( nullptr, result, tf, nullptr );
    }

    return result.c_str();
}

std::optional<drp::ArtworkFetcher::MusicBrainzFetchRequest> CreateMusicBrainzRequest( const metadb_handle_ptr& handle )
{
    if ( handle.is_empty() )
    {
        return std::nullopt;
    }

    const auto userReleaseMbid = EvaluateQueryForPlayingTrack( handle, "[$lower($if3($meta(MUSICBRAINZ_ALBUMID),$meta(MUSICBRAINZ ALBUM ID)))]" );
    const auto artist = EvaluateQueryForPlayingTrack( handle, "$if3(%album artist%,%artist%,%composer%)" );
    const auto album = EvaluateQueryForPlayingTrack( handle, "%album%" );

    return drp::ArtworkFetcher::MusicBrainzFetchRequest{
        .artist = artist,
        .album = album,
        .userReleaseMbidOpt = userReleaseMbid.empty() ? std::optional<qwr::u8string>{} : userReleaseMbid };
}

std::optional<drp::ArtworkFetcher::LocalArtworkUploadRequest> CreateLocalArtworkUploadRequest(
    const metadb_handle_ptr& handle,
    drp::artwork::LocalArtworkHost host )
{
    if ( handle.is_empty() )
    {
        return std::nullopt;
    }

    return drp::ArtworkFetcher::LocalArtworkUploadRequest{
        .artPinId = EvaluateQueryForPlayingTrack( handle, config::localArtworkPinQuery ),
        .handle = handle,
        .host = host,
        .imgurClientId = config::imgurClientId,
        .options = {
            .maxWidth = config::localArtworkUploadMaxWidth,
            .maxHeight = config::localArtworkUploadMaxHeight } };
}

std::optional<drp::ArtworkFetcher::TheAudioDbFetchRequest> CreateTheAudioDbRequest( const metadb_handle_ptr& handle )
{
    if ( handle.is_empty() )
    {
        return std::nullopt;
    }

    static std::atomic_bool hasLoggedCredentialReadFailure = false;
    std::optional<qwr::u8string> apiKey;
    try
    {
        apiKey = drp::credentials::ReadTheAudioDbApiKey();
        hasLoggedCredentialReadFailure.store( false );
    }
    catch ( const std::exception& e )
    {
        if ( !hasLoggedCredentialReadFailure.exchange( true ) )
        {
            drp::LogError( fmt::format( "Skipping TheAudioDB because its stored API key could not be read: {}", e.what() ) );
        }
        return std::nullopt;
    }
    catch ( ... )
    {
        if ( !hasLoggedCredentialReadFailure.exchange( true ) )
        {
            drp::LogError( "Skipping TheAudioDB because its stored API key could not be read" );
        }
        return std::nullopt;
    }

    if ( !apiKey )
    {
        return std::nullopt;
    }

    return drp::ArtworkFetcher::TheAudioDbFetchRequest{
        .artist = EvaluateQueryForPlayingTrack( handle, "$if3(%album artist%,%artist%,%composer%)" ),
        .album = EvaluateQueryForPlayingTrack( handle, "%album%" ),
        .apiKey = *apiKey };
}

std::optional<qwr::u8string> ResolveTrackArtUrl( const drp::internal::PresenceData& pd )
{
    if ( pd.metadb.is_empty() )
    {
        return std::nullopt;
    }

    std::vector<drp::ArtworkFetcher::FetchRequest> requests;
    if ( config::enableCatboxUpload )
    {
        const auto requestOpt = CreateLocalArtworkUploadRequest( pd.metadb, drp::artwork::LocalArtworkHost::Catbox );
        if ( requestOpt )
        {
            requests.emplace_back( *requestOpt );
        }
    }

    if ( config::enableImgurUpload )
    {
        const auto requestOpt = CreateLocalArtworkUploadRequest( pd.metadb, drp::artwork::LocalArtworkHost::Imgur );
        if ( requestOpt )
        {
            requests.emplace_back( *requestOpt );
        }
    }

    if ( config::enableAlbumArtFetch )
    {
        const auto requestOpt = CreateMusicBrainzRequest( pd.metadb );
        if ( requestOpt )
        {
            requests.emplace_back( *requestOpt );
        }
    }

    if ( config::enableTheAudioDbFetch )
    {
        const auto requestOpt = CreateTheAudioDbRequest( pd.metadb );
        if ( requestOpt )
        {
            requests.emplace_back( *requestOpt );
        }
    }

    return drp::ArtworkFetcher::Get().GetArtUrl( requests );
}

double ParseDoubleOrZero( const qwr::u8string& value )
{
    if ( value.empty() )
    {
        return 0;
    }

    try
    {
        size_t processedChars = 0;
        const auto result = std::stold( value, &processedChars );
        if ( !processedChars || !std::isfinite( result ) )
        {
            return 0;
        }

        return static_cast<double>( result );
    }
    catch ( const std::exception& )
    {
        drp::LogWarning( fmt::format( "Failed to parse playback time value: `{}`", value ) );
        return 0;
    }
}

int64_t RoundedNonNegativeSeconds( double value )
{
    if ( !std::isfinite( value ) || value <= 0 )
    {
        return 0;
    }

    constexpr auto maxSeconds = static_cast<double>( std::numeric_limits<int64_t>::max() );
    if ( value >= maxSeconds )
    {
        return std::numeric_limits<int64_t>::max();
    }

    return std::llround( value );
}

int64_t AddTimestampOffset( int64_t timestamp, int64_t offset )
{
    if ( offset > std::numeric_limits<int64_t>::max() - timestamp )
    {
        return std::numeric_limits<int64_t>::max();
    }

    return timestamp + offset;
}

int64_t CurrentUnixTime()
{
    const auto now = std::time( nullptr );
    return ( now < 0 ? 0 : static_cast<int64_t>( now ) );
}

void ApplyDiscordTextLimit( qwr::u8string& str )
{
    str = qwr::unicode::ToU8( drp::validation::TruncateDiscordText( qwr::unicode::ToWide( str ) ) );
}

} // namespace

namespace drp::internal
{

PresenceData::PresenceData()
{
    memset( &presence, 0, sizeof( presence ) );
    presence.activityType = DiscordActivityType::LISTENING;
    UpdateTextFieldPointers();
}

PresenceData::PresenceData( const PresenceData& other )
{
    CopyData( other );
}

PresenceData& PresenceData::operator=( const PresenceData& other )
{
    if ( this != &other )
    {
        CopyData( other );
    }

    return *this;
}

bool PresenceData::operator==( const PresenceData& other ) const
{
    auto areStringsSame = []( const char* a, const char* b ) {
        return ( ( a == b ) || ( a && b && !strcmp( a, b ) ) );
    };

    return areStringsSame( presence.state, other.presence.state )
           && areStringsSame( presence.details, other.presence.details )
           && areStringsSame( presence.largeImageKey, other.presence.largeImageKey )
           && areStringsSame( presence.largeImageText, other.presence.largeImageText )
           && areStringsSame( presence.smallImageKey, other.presence.smallImageKey )
           && areStringsSame( presence.smallImageText, other.presence.smallImageText )
           && presence.startTimestamp == other.presence.startTimestamp
           && presence.endTimestamp == other.presence.endTimestamp
           && trackLength == other.trackLength;
}

bool PresenceData::operator!=( const PresenceData& other ) const
{
    return !operator==( other );
}

void PresenceData::CopyData( const PresenceData& other )
{
    metadb = other.metadb;
    topText = other.topText;
    middleText = other.middleText;
    bottomText = other.bottomText;
    largeImageKey = other.largeImageKey;
    smallImageKey = other.smallImageKey;
    trackLength = other.trackLength;

    memcpy( &presence, &other.presence, sizeof( presence ) );
    UpdateTextFieldPointers();
    presence.activityType = DiscordActivityType::LISTENING;
    presence.largeImageKey = ( largeImageKey.empty() ? nullptr : largeImageKey.c_str() );
    presence.smallImageKey = ( smallImageKey.empty() ? nullptr : smallImageKey.c_str() );
}

void PresenceData::UpdateTextFieldPointers()
{
    presence.details = topText.c_str();
    presence.state = middleText.c_str();
    presence.largeImageText = bottomText.c_str();
    presence.smallImageText = nullptr;
}

} // namespace drp::internal

namespace drp
{

PresenceModifier::PresenceModifier( DiscordAdapter& parent, const drp::internal::PresenceData& presenceData )
    : parent_( parent )
    , presenceData_( presenceData )
{
}

PresenceModifier::~PresenceModifier()
{
    const bool hasChanged = HasChanged();
    if ( hasChanged )
    {
        parent_.presenceData_ = presenceData_;
    }

    const bool needsToBeDisabled = ( isDisabled_ || !playback_control::get()->is_playing() || ( playback_control::get()->is_paused() && config::disableWhenPaused ) );
    if ( needsToBeDisabled )
    {
        if ( parent_.HasPresence() )
        {
            parent_.ClearPresence();
        }
    }
    else
    {
        if ( !parent_.HasPresence() || hasChanged )
        {
            parent_.SendPresence();
        }
    }
}

void PresenceModifier::UpdateImage()
{
    auto& pd = presenceData_;

    const auto policy = artwork::NormaliseDisplayPolicy( static_cast<artwork::DisplayPolicy>( config::artworkDisplayPolicy ) );
    const bool hasArtworkSource = config::enableCatboxUpload || config::enableImgurUpload
                                  || config::enableAlbumArtFetch || config::enableTheAudioDbFetch;
    const bool shouldResolveArtwork = artwork::ShouldResolveArtwork( policy ) && pd.metadb.is_valid() && hasArtworkSource;
    if ( shouldResolveArtwork )
    {
        const auto artUrlOpt = ResolveTrackArtUrl( pd );
        if ( artUrlOpt )
        {
            SetImageKey( pd.largeImageKey, *artUrlOpt, pd.presence.largeImageKey );
            return;
        }
    }
    else
    {
        ArtworkFetcher::Get().CancelPendingRequest();
    }

    if ( !artwork::ShouldUseFallbackImage( policy ) )
    {
        SetImageKey( pd.largeImageKey, qwr::u8string{}, pd.presence.largeImageKey );
        return;
    }

    switch ( config::largeImageSettings )
    {
    case config::ImageSetting::Light:
    {
        SetImageKey( pd.largeImageKey, config::largeImageId_Light, pd.presence.largeImageKey );
        break;
    }
    case config::ImageSetting::Dark:
    {
        SetImageKey( pd.largeImageKey, config::largeImageId_Dark, pd.presence.largeImageKey );
        break;
    }
    case config::ImageSetting::Disabled:
    {
        SetImageKey( pd.largeImageKey, qwr::u8string{}, pd.presence.largeImageKey );
        break;
    }
    }
}

void PresenceModifier::UpdateSmallImage()
{
    auto& pd = presenceData_;
    auto pc = playback_control::get();

    const bool usePausedImage = ( pc->is_paused() || config::swapSmallImages );

    switch ( config::smallImageSettings )
    {
    case config::ImageSetting::Light:
    {
        SetImageKey( pd.smallImageKey,
            usePausedImage ? config::pausedImageId_Light : config::playingImageId_Light,
            pd.presence.smallImageKey );
        break;
    }
    case config::ImageSetting::Dark:
    {
        SetImageKey( pd.smallImageKey,
            usePausedImage ? config::pausedImageId_Dark : config::playingImageId_Dark,
            pd.presence.smallImageKey );
        break;
    }
    case config::ImageSetting::Disabled:
    {
        SetImageKey( pd.smallImageKey, qwr::u8string{}, pd.presence.smallImageKey );
        break;
    }
    }
}

void PresenceModifier::UpdateTrack( metadb_handle_ptr metadb )
{
    auto& pd = presenceData_;

    pd.topText.clear();
    pd.middleText.clear();
    pd.bottomText.clear();
    pd.trackLength = 0;

    if ( metadb.is_valid() )
    { // Need to save, since refresh might be required when settings are changed
        pd.metadb = metadb;
    }

    const auto queryData = [metadb = pd.metadb]( const qwr::u8string& query ) {
        return EvaluateQueryForPlayingTrack( metadb, query );
    };

    pd.topText = queryData( config::topTextQuery );
    ApplyDiscordTextLimit( pd.topText );
    pd.middleText = queryData( config::middleTextQuery );
    ApplyDiscordTextLimit( pd.middleText );
    pd.bottomText = queryData( config::bottomTextQuery );
    ApplyDiscordTextLimit( pd.bottomText );
    pd.UpdateTextFieldPointers();

    const qwr::u8string lengthStr = queryData( "[%length_seconds_fp%]" );
    const qwr::u8string durationStr = queryData( "[%playback_time_seconds%]" );
    UpdateDuration( ParseDoubleOrZero( durationStr ), ParseDoubleOrZero( lengthStr ) );

    UpdateImage();
}

void PresenceModifier::UpdateDuration( double currentTime )
{
    auto& pd = presenceData_;
    auto pc = playback_control::get();
    const config::TimeSetting timeSetting = ( ( pd.trackLength && pc->is_playing() && !pc->is_paused() ) ? config::timeSettings : config::TimeSetting::Disabled );
    const auto now = CurrentUnixTime();
    const auto currentSeconds = RoundedNonNegativeSeconds( currentTime );
    switch ( timeSetting )
    {
    case config::TimeSetting::Elapsed:
    {
        pd.presence.startTimestamp = ( currentSeconds > now ? 0 : now - currentSeconds );
        pd.presence.endTimestamp = 0;

        break;
    }
    case config::TimeSetting::Remaining:
    {
        const auto remainingSeconds = RoundedNonNegativeSeconds( pd.trackLength - currentTime );
        pd.presence.startTimestamp = 0;
        pd.presence.endTimestamp = AddTimestampOffset( now, remainingSeconds );

        break;
    }
    case config::TimeSetting::Disabled:
    {
        pd.presence.startTimestamp = 0;
        pd.presence.endTimestamp = 0;

        break;
    }
    }
}

void PresenceModifier::UpdateDuration( double currentTime, double totalLength )
{
    auto& pd = presenceData_;
    pd.trackLength = ( std::isfinite( totalLength ) && totalLength > 0 ? totalLength : 0 );
    UpdateDuration( currentTime );
}

void PresenceModifier::DisableDuration()
{
    auto& pd = presenceData_;
    pd.presence.startTimestamp = 0;
    pd.presence.endTimestamp = 0;
}

void PresenceModifier::SetImageKey( qwr::u8string& localKey, const qwr::u8string& imageKey, const char*& destination )
{
    localKey = imageKey;
    destination = localKey.empty() ? nullptr : localKey.c_str();
}

bool PresenceModifier::HasChanged() const
{
    return ( parent_.presenceData_ != presenceData_ );
}

void PresenceModifier::Rollback()
{
    presenceData_ = parent_.presenceData_;
}

void PresenceModifier::Disable()
{
    isDisabled_ = true;
}

} // namespace drp
