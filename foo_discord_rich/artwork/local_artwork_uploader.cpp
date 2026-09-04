#include <stdafx.h>

#include "local_artwork_uploader.h"

#include <utils/validation.h>

#include <cmath>
#include <memory>
#include <vector>

#include <cpr/cpr.h>
#include <webp/encode.h>
#include <wincodec.h>
#include <wrl/client.h>

#pragma comment( lib, "windowscodecs.lib" )
#pragma comment( lib, "ole32.lib" )

namespace
{

const cpr::Timeout kRequestTimeout{ 120000 };
const cpr::ConnectTimeout kConnectTimeout{ 5000 };
constexpr t_size kMaxArtworkUploadBytes = 20 * 1024 * 1024;
constexpr t_size kMaxArtworkInputBytes = 64 * 1024 * 1024;
const cpr::Header kRequestHeaders{
    { "User-Agent", DRP_UNDERSCORE_NAME "/" DRP_VERSION " (" DRP_HOMEPAGE ")" },
};

struct ArtworkUploadData
{
    std::vector<char> bytes;
    qwr::u8string filename;
};

using Microsoft::WRL::ComPtr;

class ComInitialiser
{
public:
    ComInitialiser()
    {
        const auto result = CoInitializeEx( nullptr, COINIT_MULTITHREADED );
        if ( SUCCEEDED( result ) )
        {
            shouldUninitialise_ = true;
        }
        else if ( result != RPC_E_CHANGED_MODE )
        {
            throw qwr::QwrException( "Could not initialise image processing (0x{:08X})", static_cast<unsigned long>( result ) );
        }
    }

    ~ComInitialiser()
    {
        if ( shouldUninitialise_ )
        {
            CoUninitialize();
        }
    }

private:
    bool shouldUninitialise_ = false;
};

void ThrowIfFailed( HRESULT result, qwr::u8string_view operation )
{
    if ( FAILED( result ) )
    {
        throw qwr::QwrException( "Could not {} (0x{:08X})", operation, static_cast<unsigned long>( result ) );
    }
}

ArtworkUploadData CreateWebPArtworkUploadData(
    const album_art_data_ptr& artwork,
    const drp::artwork::LocalArtworkUploadOptions& options,
    abort_callback& aborter )
{
    if ( !drp::artwork::AreValidLocalArtworkUploadOptions( options ) )
    {
        throw qwr::QwrException( "Local artwork upload dimensions must be between 1 and 4096 pixels" );
    }

    aborter.check();
    ComInitialiser comInitialiser;
    ComPtr<IWICImagingFactory> factory;
    ThrowIfFailed(
        CoCreateInstance( CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS( &factory ) ),
        "create the Windows image processor" );

    ComPtr<IStream> inputStream;
    ThrowIfFailed( CreateStreamOnHGlobal( nullptr, TRUE, &inputStream ), "load local artwork" );
    ULONG bytesWritten = 0;
    ThrowIfFailed(
        inputStream->Write( artwork->get_ptr(), static_cast<ULONG>( artwork->get_size() ), &bytesWritten ),
        "load local artwork" );
    if ( bytesWritten != artwork->get_size() )
    {
        throw qwr::QwrException( "Could not load local artwork" );
    }

    LARGE_INTEGER start{};
    ThrowIfFailed( inputStream->Seek( start, STREAM_SEEK_SET, nullptr ), "load local artwork" );
    ComPtr<IWICBitmapDecoder> decoder;
    ThrowIfFailed(
        factory->CreateDecoderFromStream( inputStream.Get(), nullptr, WICDecodeMetadataCacheOnLoad, &decoder ),
        "decode local artwork" );
    ComPtr<IWICBitmapFrameDecode> inputFrame;
    ThrowIfFailed( decoder->GetFrame( 0, &inputFrame ), "decode local artwork" );

    UINT sourceWidth = 0;
    UINT sourceHeight = 0;
    ThrowIfFailed( inputFrame->GetSize( &sourceWidth, &sourceHeight ), "read local artwork dimensions" );
    if ( sourceWidth == 0 || sourceHeight == 0 )
    {
        throw qwr::QwrException( "Local artwork has invalid dimensions" );
    }
    const auto scale = std::min(
        1.0,
        std::min(
            static_cast<double>( options.maxWidth ) / sourceWidth,
            static_cast<double>( options.maxHeight ) / sourceHeight ) );
    const auto targetWidth = static_cast<UINT>( std::max( 1.0, std::round( sourceWidth * scale ) ) );
    const auto targetHeight = static_cast<UINT>( std::max( 1.0, std::round( sourceHeight * scale ) ) );

    ComPtr<IWICBitmapScaler> scaler;
    IWICBitmapSource* source = inputFrame.Get();
    if ( targetWidth != sourceWidth || targetHeight != sourceHeight )
    {
        ThrowIfFailed( factory->CreateBitmapScaler( &scaler ), "resize local artwork" );
        ThrowIfFailed(
            scaler->Initialize( inputFrame.Get(), targetWidth, targetHeight, WICBitmapInterpolationModeFant ),
            "resize local artwork" );
        source = scaler.Get();
    }

    ComPtr<IWICFormatConverter> converter;
    ThrowIfFailed( factory->CreateFormatConverter( &converter ), "prepare local artwork for WebP encoding" );
    ThrowIfFailed(
        converter->Initialize(
            source,
            GUID_WICPixelFormat32bppBGRA,
            WICBitmapDitherTypeNone,
            nullptr,
            0.0,
            WICBitmapPaletteTypeCustom ),
        "prepare local artwork for WebP encoding" );

    const auto rowBytes = static_cast<size_t>( targetWidth ) * 4;
    const auto imageBytes = rowBytes * targetHeight;
    std::vector<uint8_t> pixels( imageBytes );
    ThrowIfFailed(
        converter->CopyPixels(
            nullptr,
            static_cast<UINT>( rowBytes ),
            static_cast<UINT>( imageBytes ),
            pixels.data() ),
        "read resized local artwork" );
    uint8_t* encoded = nullptr;
    const auto encodedBytes = WebPEncodeBGRA(
        pixels.data(),
        static_cast<int>( targetWidth ),
        static_cast<int>( targetHeight ),
        static_cast<int>( rowBytes ),
        85.0f,
        &encoded );
    if ( encodedBytes == 0 || !encoded )
    {
        throw qwr::QwrException( "Could not encode local artwork as WebP" );
    }
    const auto encodedImage = std::unique_ptr<uint8_t, decltype( &WebPFree )>( encoded, &WebPFree );
    if ( encodedBytes > kMaxArtworkUploadBytes )
    {
        throw qwr::QwrException( "Processed local artwork exceeds the {} MiB upload limit", kMaxArtworkUploadBytes / 1024 / 1024 );
    }
    aborter.check();

    return ArtworkUploadData{
        .bytes = { reinterpret_cast<const char*>( encodedImage.get() ), reinterpret_cast<const char*>( encodedImage.get() ) + encodedBytes },
        .filename = "cover.webp" };
}

std::optional<ArtworkUploadData> GetArtworkUploadData(
    const metadb_handle_ptr& handle,
    const drp::artwork::LocalArtworkUploadOptions& options,
    abort_callback& aborter )
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
    if ( artwork->get_size() > kMaxArtworkInputBytes )
    {
        throw qwr::QwrException( "Local artwork exceeds the {} MiB processing limit", kMaxArtworkInputBytes / 1024 / 1024 );
    }

    return CreateWebPArtworkUploadData( artwork, options, aborter );
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
    const LocalArtworkUploadOptions& options,
    abort_callback& aborter )
{
    try
    {
        const auto artwork = GetArtworkUploadData( handle, options, aborter );
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
