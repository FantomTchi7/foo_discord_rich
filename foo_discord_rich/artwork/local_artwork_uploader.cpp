#include <stdafx.h>

#include "local_artwork_uploader.h"

#include <component_paths.h>
#include <utils/validation.h>

#include <cpr/cpr.h>
#include <qwr/final_action.h>

namespace
{

namespace fs = std::filesystem;

const cpr::Timeout kRequestTimeout{ 15000 };
const cpr::ConnectTimeout kConnectTimeout{ 5000 };
const cpr::Header kRequestHeaders{
    { "User-Agent", DRP_UNDERSCORE_NAME "/" DRP_VERSION " (" DRP_HOMEPAGE ")" },
};

struct ArtFile
{
    qwr::u8string path;
    bool isTemporary = false;
};

qwr::u8string CreateTemporaryImagePath( qwr::u8string_view extension )
{
    fs::create_directories( drp::path::ImageDir() );
    for ( unsigned attempt = 0; attempt < 100; ++attempt )
    {
        const auto path = drp::path::ImageDir() / fmt::format(
                              "upload-{}-{}-{}.{}",
                              GetCurrentProcessId(),
                              GetTickCount64(),
                              attempt,
                              extension );
        if ( !fs::exists( path ) )
        {
            return path.u8string();
        }
    }
    throw qwr::QwrException( "Failed to allocate a unique temporary artwork path" );
}

ArtFile GetArtworkFile( const metadb_handle_ptr& handle, abort_callback& aborter )
{
    if ( handle.is_empty() )
    {
        return {};
    }

    const auto artType = album_art_ids::cover_front;
    const auto handles = pfc::list_single_ref_t<metadb_handle_ptr>( handle );
    const auto types = pfc::list_single_ref_t<GUID>( artType );
    auto extractor = album_art_manager_v3::get()->open_v3( handles, types, nullptr, aborter );
    if ( !extractor.is_valid() )
    {
        return {};
    }

    const auto data = extractor->query( artType, aborter );
    if ( !data.is_valid() )
    {
        return {};
    }

    const auto paths = extractor->query_paths( artType, aborter );
    if ( paths.is_valid() && paths->get_count() )
    {
        qwr::u8string path = paths->get_path( 0 );
        constexpr qwr::u8string_view kFileUrlPrefix = "file://";
        if ( path.starts_with( kFileUrlPrefix ) )
        {
            path = path.substr( kFileUrlPrefix.size() );
        }
        if ( path != handle->get_location().get_path() && fs::is_regular_file( fs::u8path( path ) ) )
        {
            return { std::move( path ), false };
        }
    }

    qwr::u8string extension = "jpg";
    try
    {
        const auto info = fb2k::imageLoaderLite::get()->getInfo( data->get_ptr(), data->get_size(), aborter );
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

    const auto imagePath = CreateTemporaryImagePath( extension );
    try
    {
        service_ptr_t<file> file;
        filesystem::g_open_write_new( file, imagePath.c_str(), aborter );
        file->write( data->get_ptr(), data->get_size(), aborter );
    }
    catch ( const pfc::exception& e )
    {
        throw qwr::QwrException( "Failed to save temporary image file: {}", e.what() );
    }
    return { imagePath, true };
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

qwr::u8string UploadToCatbox( const ArtFile& file, abort_callback& aborter )
{
    const auto response = cpr::Post(
        cpr::Url{ "https://catbox.moe/user/api.php" },
        cpr::Multipart{ { "reqtype", "fileupload" }, { "fileToUpload", cpr::File{ file.path } } },
        kRequestHeaders,
        kConnectTimeout,
        kRequestTimeout,
        CreateAbortProgress( aborter ) );
    aborter.check();
    if ( response.status_code != 200 )
    {
        throw qwr::QwrException( "Catbox upload failed with HTTP {}: {}", response.status_code, response.reason );
    }
    return RequireSecureUrl( response.text, "Catbox" );
}

qwr::u8string UploadToImgur( const ArtFile& file, qwr::u8string_view clientId, abort_callback& aborter )
{
    if ( clientId.empty() )
    {
        throw qwr::QwrException( "Imgur requires a Client ID" );
    }
    auto headers = kRequestHeaders;
    headers.emplace( "Authorization", fmt::format( "Client-ID {}", clientId ) );
    const auto response = cpr::Post(
        cpr::Url{ "https://api.imgur.com/3/image" },
        cpr::Multipart{ { "image", cpr::File{ file.path } } },
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
    ArtFile file;
    try
    {
        file = GetArtworkFile( handle, aborter );
    }
    catch ( const exception_album_art_not_found& )
    {
        return std::nullopt;
    }
    if ( file.path.empty() )
    {
        return std::nullopt;
    }

    const qwr::final_action cleanup( [&] {
        if ( file.isTemporary )
        {
            std::error_code error;
            fs::remove( fs::u8path( file.path ), error );
            if ( error )
            {
                LogWarning( fmt::format( "Failed to remove temporary artwork: {}", error.message() ) );
            }
        }
    } );

    return host == LocalArtworkHost::Catbox
               ? UploadToCatbox( file, aborter )
               : UploadToImgur( file, imgurClientId, aborter );
}

} // namespace drp::artwork
