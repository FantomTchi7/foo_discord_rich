#pragma once

#include <compare>
#include <cstdint>

#include <foobar2000/SDK/abort_callback.h>

#include <optional>

namespace drp::artwork
{

enum class LocalArtworkHost
{
    Catbox,
    Imgur
};

struct LocalArtworkUploadOptions
{
    uint32_t maxWidth = 250;
    uint32_t maxHeight = 250;

    auto operator<=>( const LocalArtworkUploadOptions& other ) const = default;
};

constexpr bool AreValidLocalArtworkUploadOptions( const LocalArtworkUploadOptions& options ) noexcept
{
    constexpr uint32_t kMaximumDimension = 4096;
    return options.maxWidth > 0 && options.maxWidth <= kMaximumDimension
           && options.maxHeight > 0 && options.maxHeight <= kMaximumDimension;
}

/// @throw qwr::QwrException
/// @throw exception_aborted
std::optional<qwr::u8string> UploadLocalArtwork(
    const metadb_handle_ptr& handle,
    LocalArtworkHost host,
    qwr::u8string_view imgurClientId,
    const LocalArtworkUploadOptions& options,
    abort_callback& aborter );

} // namespace drp::artwork
