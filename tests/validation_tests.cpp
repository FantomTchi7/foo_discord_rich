#include <utils/artwork_cache_key.h>
#include <utils/validation.h>
#include <utils/artwork_policy.h>
#include <utils/discord_application_id_migration.h>
#include <utils/theaudiodb.h>

#include <cassert>

int main()
{
    using drp::artwork::DisplayPolicy;
    using drp::artwork::BuildMusicBrainzCacheKey;
    using drp::artwork::BuildTheAudioDbCacheKey;
    using drp::artwork::BuildCatboxCacheKey;
    using drp::artwork::BuildImgurCacheKey;
    using drp::artwork::BoundedTheAudioDbKeySet;
    using drp::artwork::CanCommitCacheResult;
    using drp::artwork::CanPublishRequestResult;
    using drp::artwork::IsEligibleTheAudioDbSupporterKey;
    using drp::artwork::IsValidDisplayPolicy;
    using drp::artwork::IsQualifiedCacheKey;
    using drp::artwork::NormaliseDisplayPolicy;
    using drp::artwork::ShouldResolveArtwork;
    using drp::artwork::ShouldUseFallbackImage;
    using drp::artwork::IsValidTheAudioDbApiKey;
    using drp::artwork::IsTheAudioDbLookupMatch;
    using drp::artwork::NormaliseTheAudioDbLookupText;
    using drp::artwork::SelectTheAudioDbArtworkUrl;
    using drp::artwork::TheAudioDbAlbumCandidate;
    using drp::config::ShouldMigrateLegacyDiscordApplicationId;
    using drp::validation::IsSecureImageUrl;
    using drp::validation::IsCacheEntryFresh;
    using drp::validation::TruncateDiscordText;

    assert( ShouldResolveArtwork( DisplayPolicy::PreferArtwork ) );
    assert( ShouldUseFallbackImage( DisplayPolicy::PreferArtwork ) );
    assert( !ShouldResolveArtwork( DisplayPolicy::ApplicationIcon ) );
    assert( ShouldUseFallbackImage( DisplayPolicy::ApplicationIcon ) );
    assert( ShouldResolveArtwork( DisplayPolicy::ArtworkOnly ) );
    assert( !ShouldUseFallbackImage( DisplayPolicy::ArtworkOnly ) );
    assert( IsValidDisplayPolicy( DisplayPolicy::PreferArtwork ) );
    assert( IsValidDisplayPolicy( DisplayPolicy::ApplicationIcon ) );
    assert( IsValidDisplayPolicy( DisplayPolicy::ArtworkOnly ) );
    assert( !IsValidDisplayPolicy( static_cast<DisplayPolicy>( 255 ) ) );
    assert( NormaliseDisplayPolicy( static_cast<DisplayPolicy>( 255 ) ) == DisplayPolicy::PreferArtwork );

    assert( ShouldMigrateLegacyDiscordApplicationId( false, "507982587416018945" ) );
    assert( !ShouldMigrateLegacyDiscordApplicationId( false, "custom-application-id" ) );
    assert( !ShouldMigrateLegacyDiscordApplicationId( false, "1536157545863847938" ) );
    assert( !ShouldMigrateLegacyDiscordApplicationId( true, "507982587416018945" ) );

    assert( BuildMusicBrainzCacheKey( "artist", "album", std::nullopt ) == "mb:metadata:6:artist:5:album" );
    constexpr auto upperMbid = "A0B1C2D3-E4F5-6789-ABCD-EF0123456789";
    constexpr auto lowerMbid = "a0b1c2d3-e4f5-6789-abcd-ef0123456789";
    constexpr auto otherMbid = "b0b1c2d3-e4f5-6789-abcd-ef0123456789";
    assert( BuildMusicBrainzCacheKey( "", "", std::optional<std::string>{ upperMbid } ) == std::string{ "mb:release:" } + lowerMbid );
    assert( BuildMusicBrainzCacheKey( "artist", "album", std::optional<std::string>{ upperMbid } )
            == BuildMusicBrainzCacheKey( "artist", "album", std::optional<std::string>{ lowerMbid } ) );
    assert( BuildMusicBrainzCacheKey( "artist", "album", std::optional<std::string>{ upperMbid } )
            != BuildMusicBrainzCacheKey( "artist", "album", std::optional<std::string>{ otherMbid } ) );
    assert( BuildMusicBrainzCacheKey( "artist", "album", std::optional<std::string>{ "not-an-mbid" } )
            == BuildMusicBrainzCacheKey( "artist", "album", std::nullopt ) );
    assert( !BuildMusicBrainzCacheKey( "", "album", std::optional<std::string>{ "not-an-mbid" } ) );
    assert( !BuildMusicBrainzCacheKey( "", "album", std::nullopt ) );
    assert( BuildMusicBrainzCacheKey( "a|b", "c", std::nullopt ) != BuildMusicBrainzCacheKey( "a", "b|c", std::nullopt ) );
    assert( BuildCatboxCacheKey( "artist|album", 250, 250 ) == "catbox:webp:250x250:artist|album" );
    assert( BuildImgurCacheKey( "artist|album", 250, 250 ) == "imgur:webp:250x250:artist|album" );
    assert( BuildCatboxCacheKey( "artist|album", 250, 250 ) != BuildImgurCacheKey( "artist|album", 250, 250 ) );
    assert( BuildCatboxCacheKey( "artist|album", 250, 250 ) != BuildCatboxCacheKey( "artist|album", 500, 500 ) );
    assert( BuildCatboxCacheKey( "artist|album", 250, 250 ) != BuildMusicBrainzCacheKey( "artist", "album", std::nullopt ) );
    assert( !BuildCatboxCacheKey( "", 250, 250 ) );
    assert( !BuildCatboxCacheKey( "artist|album", 0, 250 ) );
    assert( BuildTheAudioDbCacheKey( "artist", "album" ) == "tadb:metadata:6:artist:5:album" );
    assert( BuildTheAudioDbCacheKey( "a|b", "c" ) != BuildTheAudioDbCacheKey( "a", "b|c" ) );
    assert( !BuildTheAudioDbCacheKey( "", "album" ) );
    assert( IsQualifiedCacheKey( std::string{ "mb:release:" } + lowerMbid ) );
    assert( IsQualifiedCacheKey( "mb:metadata:6:artist:5:album" ) );
    assert( IsQualifiedCacheKey( "catbox:webp:250x250:artist|album" ) );
    assert( IsQualifiedCacheKey( "imgur:webp:250x250:artist|album" ) );
    assert( IsQualifiedCacheKey( "tadb:metadata:6:artist:5:album" ) );
    assert( !IsQualifiedCacheKey( "artist|album" ) );
    assert( !IsQualifiedCacheKey( "upload:" ) );
    assert( CanCommitCacheResult( 4, 4 ) );
    assert( !CanCommitCacheResult( 4, 5 ) );
    assert( CanPublishRequestResult( 4, std::optional<uint64_t>{ 4 } ) );
    assert( !CanPublishRequestResult( 4, std::optional<uint64_t>{ 5 } ) );
    assert( !CanPublishRequestResult( 4, std::nullopt ) );

    assert( IsValidTheAudioDbApiKey( "123" ) );
    assert( IsValidTheAudioDbApiKey( "premium_Key-42" ) );
    assert( !IsValidTheAudioDbApiKey( "" ) );
    assert( !IsValidTheAudioDbApiKey( "bad/key" ) );
    assert( !IsValidTheAudioDbApiKey( std::string( 129, 'a' ) ) );
    assert( !IsEligibleTheAudioDbSupporterKey( "" ) );
    assert( !IsEligibleTheAudioDbSupporterKey( "123" ) );
    assert( !IsEligibleTheAudioDbSupporterKey( "bad/key" ) );
    assert( IsEligibleTheAudioDbSupporterKey( "premium_Key-42" ) );
    BoundedTheAudioDbKeySet rejectedKeys{ 2 };
    rejectedKeys.Insert( "first" );
    rejectedKeys.Insert( "second" );
    rejectedKeys.Insert( "second" );
    assert( rejectedKeys.Size() == 2 );
    assert( rejectedKeys.Contains( "first" ) );
    rejectedKeys.Insert( "third" );
    assert( rejectedKeys.Size() == 2 );
    assert( !rejectedKeys.Contains( "first" ) );
    assert( rejectedKeys.Contains( "second" ) );
    assert( rejectedKeys.Contains( "third" ) );
    assert( rejectedKeys.Erase( "second" ) );
    assert( !rejectedKeys.Erase( "second" ) );
    assert( rejectedKeys.Size() == 1 );
    assert( NormaliseTheAudioDbLookupText( "  ColdPlay\r\n" ) == "coldplay" );
    assert( NormaliseTheAudioDbLookupText( "Parachutes" ) == "parachutes" );
    assert( NormaliseTheAudioDbLookupText( " \t " ).empty() );
    assert( IsTheAudioDbLookupMatch( L"  BJ\u00D6RK ", L"Bj\u00F6rk" ) );
    assert( IsTheAudioDbLookupMatch( L"Bjo\u0308rk", L"BJ\u00D6RK" ) );
    assert( !IsTheAudioDbLookupMatch( L"Bj\u00F6rk", L"Bork" ) );
    const std::vector<TheAudioDbAlbumCandidate> audioDbCandidates{
        { L"Wrong Artist", L"Homogenic", "https://example.test/wrong-hq.jpg", "https://example.test/wrong.jpg" },
        { L"BJ\u00D6RK", L"Homogenic", "http://example.test/bad-hq.jpg", "https://example.test/valid.jpg" },
    };
    assert( SelectTheAudioDbArtworkUrl( audioDbCandidates, L"Bj\u00F6rk", L"Homogenic" ) == "https://example.test/valid.jpg" );
    assert( !SelectTheAudioDbArtworkUrl( audioDbCandidates, L"Bj\u00F6rk", L"Vespertine" ) );

    assert( IsSecureImageUrl( "https://example.test/art.jpg" ) );
    assert( !IsSecureImageUrl( "http://example.test/art.jpg" ) );
    assert( !IsSecureImageUrl( "https://example.test/bad url.jpg" ) );
    assert( !IsSecureImageUrl( std::string( "https://" ) + std::string( 247, 'a' ) ) );

    assert( IsCacheEntryFresh( 100, 160, 60 ) );
    assert( !IsCacheEntryFresh( 100, 161, 60 ) );
    assert( !IsCacheEntryFresh( 0, 160, 60 ) );
    assert( !IsCacheEntryFresh( 200, 160, 60 ) );

    assert( TruncateDiscordText( L"x" ) == L"x " );
    assert( TruncateDiscordText( std::wstring( 127, L'x' ) ).size() == 127 );
    assert( TruncateDiscordText( std::wstring( 128, L'x' ) ) == std::wstring( 124, L'x' ) + L"..." );

    std::wstring surrogateBoundary( 123, L'x' );
    surrogateBoundary.push_back( 0xD83D );
    surrogateBoundary.push_back( 0xDE00 );
    surrogateBoundary.append( 10, L'x' );
    const auto truncated = TruncateDiscordText( surrogateBoundary );
    assert( truncated.size() == 126 );
    assert( truncated.substr( truncated.size() - 3 ) == L"..." );
    return 0;
}
