#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace drp::artwork
{

inline constexpr int kArtworkCacheFormatVersion = 5;

inline bool IsCanonicalMbid( std::string_view value )
{
    if ( value.size() != 36 )
    {
        return false;
    }

    for ( size_t i = 0; i < value.size(); ++i )
    {
        if ( i == 8 || i == 13 || i == 18 || i == 23 )
        {
            if ( value[i] != '-' )
            {
                return false;
            }
            continue;
        }

        const auto ch = value[i];
        const bool isHexDigit = ( ch >= '0' && ch <= '9' ) || ( ch >= 'a' && ch <= 'f' ) || ( ch >= 'A' && ch <= 'F' );
        if ( !isHexDigit )
        {
            return false;
        }
    }

    return true;
}

inline std::optional<std::string> BuildMusicBrainzCacheKey(
    const std::string& artist,
    const std::string& album,
    const std::optional<std::string>& releaseMbidOpt )
{
    if ( releaseMbidOpt && IsCanonicalMbid( *releaseMbidOpt ) )
    {
        auto normalisedMbid = *releaseMbidOpt;
        for ( auto& ch: normalisedMbid )
        {
            if ( ch >= 'A' && ch <= 'Z' )
            {
                ch = static_cast<char>( ch - 'A' + 'a' );
            }
        }
        return "mb:release:" + normalisedMbid;
    }

    if ( artist.empty() || album.empty() )
    {
        return std::nullopt;
    }

    return "mb:metadata:" + std::to_string( artist.size() ) + ":" + artist + ":" + std::to_string( album.size() ) + ":" + album;
}

inline std::optional<std::string> BuildLocalArtworkCacheKey( std::string_view provider, const std::string& artPinId )
{
    if ( provider.empty() || artPinId.empty() )
    {
        return std::nullopt;
    }

    return std::string{ provider } + ":" + artPinId;
}

inline std::optional<std::string> BuildCatboxCacheKey( const std::string& artPinId )
{
    return BuildLocalArtworkCacheKey( "catbox", artPinId );
}

inline std::optional<std::string> BuildImgurCacheKey( const std::string& artPinId )
{
    return BuildLocalArtworkCacheKey( "imgur", artPinId );
}

inline std::optional<std::string> BuildTheAudioDbCacheKey( const std::string& artist, const std::string& album )
{
    if ( artist.empty() || album.empty() )
    {
        return std::nullopt;
    }

    return "tadb:metadata:" + std::to_string( artist.size() ) + ":" + artist + ":" + std::to_string( album.size() ) + ":" + album;
}

inline bool IsQualifiedCacheKey( std::string_view key )
{
    constexpr std::string_view releasePrefix = "mb:release:";
    constexpr std::string_view metadataPrefix = "mb:metadata:";
    constexpr std::string_view catboxPrefix = "catbox:";
    constexpr std::string_view imgurPrefix = "imgur:";
    constexpr std::string_view theAudioDbPrefix = "tadb:metadata:";

    if ( key.starts_with( releasePrefix ) )
    {
        return IsCanonicalMbid( key.substr( releasePrefix.size() ) );
    }

    return ( key.starts_with( metadataPrefix ) && key.size() > metadataPrefix.size() )
           || ( key.starts_with( catboxPrefix ) && key.size() > catboxPrefix.size() )
           || ( key.starts_with( imgurPrefix ) && key.size() > imgurPrefix.size() )
           || ( key.starts_with( theAudioDbPrefix ) && key.size() > theAudioDbPrefix.size() );
}

constexpr bool CanCommitCacheResult( uint64_t workGeneration, uint64_t currentGeneration ) noexcept
{
    return workGeneration == currentGeneration;
}

constexpr bool CanPublishRequestResult( uint64_t workGeneration, std::optional<uint64_t> currentGeneration ) noexcept
{
    return currentGeneration && workGeneration == *currentGeneration;
}

} // namespace drp::artwork
