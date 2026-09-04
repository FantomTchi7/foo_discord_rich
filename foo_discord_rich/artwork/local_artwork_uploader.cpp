#include <stdafx.h>

#include "local_artwork_uploader.h"

#include <utils/validation.h>

#include <cpr/cpr.h>

namespace
{

const cpr::Timeout kRequestTimeout{ 15000 };
const cpr::ConnectTimeout kConnectTimeout{ 5000 };
constexpr t_size kMaxArtworkUploadBytes = 20 * 1024 * 1024;
const cpr::Header kRequestHeaders{
    { "User-Agent", DRP_UNDERSCORE_NAME "/" DRP_VERSION " (" DRP_HOMEPAGE ")" },
};

struct ArtworkUploadData
{
    std::vector<char> bytes;
    qwr::u8string filename;
};

qwr::u8string GetArtworkFilename( const album_art_data_ptr& artwork, abort_callback& aborter )
{
    qwr::u8string extension = "jpg";
    try
    {
        const auto info = fb2k::imageLoaderLite::get()->getInfo( artwork->get_ptr(), artwork->get_size(), aborter );
        const qwr::u8string_view mime = info.mime ? info.mime : "";
        if ( mime == "image/png" )
        {
            extension = "png";
        }
        else if ( mime == "image/gif" )
        {
            extension = "gif";
        }
        else if ( mime == "image/webp" )
        {
            extension = "webp";
        }
        else if ( mime == "image/bmp" )
        {
            extension = "bmp";
        }
    }
    catch ( const pfc::exception& )
    {
    }

    return "cover." + extension;
}

std::optional<ArtworkUploadData> GetArtworkUploadData( const metadb_handle_ptr& handle, abort_callback& aborter )
{
    if ( handle.is_empty() )
    {
        return std::nullopt;
    }

    const auto artType = album_art_ids::cover_front;
    const auto handles = pfc::list_single_ref_t<metadb_handle_ptr>( handle );
    const auto types = pfc::list_single_ref_t<GUID>( artType );
    auto extractor = album_art_manager_v3::get()->open_v3( handles, types, nullptr, aborter );
    if ( !extractor.is_valid() )
    {
        return std::nullopt;
    }

    const auto artwork = extractor->query( artType, aborter );
    if ( !artwork.is_valid() )
    {
        return std::nullopt;
    }
    if ( artwork->get_size() > kMaxArtworkUploadBytes )
    {
        throw qwr::QwrException( "Local artwork exceeds the {} MiB upload limit", kMaxArtworkUploadBytes / 1024 / 1024 );
    }

    const auto* begin = static_cast<const char*>( artwork->get_ptr() );
    return ArtworkUploadData{
        .bytes = { begin, begin + artwork->get_size() },
        .filename = GetArtworkFilename( artwork, aborter ) };
}

cpr::ProgressCallback CreateAbortProgress( abort_callback& aborter )
{
    return { [&aborter]( cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, cpr::cpr_pf_arg_t, intptr_t ) {
        return !aborter.is_aborting();
    } };
}

qwr::u8string RequireSecureUrl( qwr::u8string value, qwr::u8string_view host )
{
    if ( !drp::validation::IsSecureImageUrl( value ) )
    {
        throw qwr::QwrException( "{} returned an invalid image URL", host );
    }
    return value;
}

qwr::u8string UploadToCatbox( const ArtworkUploadData& artwork, abort_callback& aborter )
{
    const cpr::Buffer image{ artwork.bytes.cbegin(), artwork.bytes.cend(), cpr::fs::path{ artwork.filename } };
    const auto response = cpr::Post(
        cpr::Url{ "https://catbox.moe/user/api.php" },
        cpr::Multipart{ { "reqtype", "fileupload" }, { "fileToUpload", image } },
        kRequestHeaders,
        kConnectTimeout,
        kRequestTimeout,
        cpr::HttpVersion{ cpr::HttpVersionCode::VERSION_1_1 },
        CreateAbortProgress( aborter ) );
    aborter.check();
    if ( response.status_code != 200 )
    {
        if ( response.error )
        {
            throw qwr::QwrException( "Catbox upload did not receive a response: {}", response.error.message.empty() ? "connection failed" : response.error.message );
        }
        throw qwr::QwrException( "Catbox upload failed with HTTP {}: {}", response.status_code, response.reason );
    }
    return RequireSecureUrl( response.text, "Catbox" );
}

qwr::u8string UploadToImgur( const ArtworkUploadData& artwork, qwr::u8string_view clientId, abort_callback& aborter )
{
    if ( clientId.empty() )
    {
        throw qwr::QwrException( "Imgur requires a Client ID" );
    }
    auto headers = kRequestHeaders;
    headers.emplace( "Authorization", fmt::format( "Client-ID {}", clientId ) );
    const cpr::Buffer image{ artwork.bytes.cbegin(), artwork.bytes.cend(), cpr::fs::path{ artwork.filename } };
    const auto response = cpr::Post(
        cpr::Url{ "https://api.imgur.com/3/image" },
        cpr::Multipart{ { "image", image } },
        headers,
        kConnectTimeout,
        kRequestTimeout,
        CreateAbortProgress( aborter ) );
    aborter.check();
    if ( response.status_code != 200 )
    {
        throw qwr::QwrException( "Imgur upload failed with HTTP {}: {}", response.status_code, response.reason );
    }
    try
    {
        const auto root = nlohmann::json::parse( response.text );
        const auto link = root.at( "data" ).at( "link" ).get<qwr::u8string>();
        return RequireSecureUrl( link, "Imgur" );
    }
    catch ( const nlohmann::json::exception& e )
    {
        throw qwr::QwrException( "Imgur returned an invalid upload response: {}", e.what() );
    }
}

} // namespace

namespace drp::artwork
{

std::optional<qwr::u8string> UploadLocalArtwork(
    const metadb_handle_ptr& handle,
    LocalArtworkHost host,
    qwr::u8string_view imgurClientId,
    abort_callback& aborter )
{
    try
    {
        const auto artwork = GetArtworkUploadData( handle, aborter );
        if ( !artwork )
        {
            return std::nullopt;
        }
        return host == LocalArtworkHost::Catbox
                   ? UploadToCatbox( *artwork, aborter )
                   : UploadToImgur( *artwork, imgurClientId, aborter );
    }
    catch ( const exception_album_art_not_found& )
    {
        return std::nullopt;
    }
}

} // namespace drp::artwork
