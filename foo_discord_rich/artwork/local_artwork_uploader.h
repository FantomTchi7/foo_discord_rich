#pragma once

#include <foobar2000/SDK/abort_callback.h>

#include <optional>

namespace drp::artwork
{

enum class LocalArtworkHost
{
    Catbox,
    Imgur
};

/// @throw qwr::QwrException
/// @throw exception_aborted
std::optional<qwr::u8string> UploadLocalArtwork(
    const metadb_handle_ptr& handle,
    LocalArtworkHost host,
    qwr::u8string_view imgurClientId,
    abort_callback& aborter );

} // namespace drp::artwork
