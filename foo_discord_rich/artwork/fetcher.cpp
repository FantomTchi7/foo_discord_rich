#include <stdafx.h>

#include "fetcher.h"

#include <artwork/musicbrainz_fetcher.h>
#include <artwork/theaudiodb_fetcher.h>
#include <artwork/local_artwork_uploader.h>
#include <discord/discord_integration.h>

#include <component_paths.h>

#include <cpr/cpr.h>
#include <qwr/abort_callback.h>
#include <qwr/algorithm.h>
#include <qwr/file_helpers.h>
#include <qwr/thread_name_setter.h>
#include <qwr/visitor.h>
#include <utils/artwork_cache_key.h>
#include <utils/theaudiodb.h>
#include <utils/validation.h>

#include <type_traits>

namespace fs = std::filesystem;

namespace
{

const std::chrono::seconds kRequestProcessingDelay{ 2 };
const std::chrono::seconds kPositiveCacheLifetime{ std::chrono::hours{ 24 * 30 } };
const std::chrono::seconds kNegativeCacheLifetime{ std::chrono::hours{ 6 } };
const std::chrono::seconds kProviderFailureCooldown{ 60 };
constexpr std::uintmax_t kMaxCacheFileBytes = 4 * 1024 * 1024;
constexpr size_t kMaxCacheKeyBytes = 1024;
constexpr size_t kMaxCacheEntries = 2048;

}

namespace
{

std::optional<qwr::u8string> GenerateCacheKey( const drp::ArtworkFetcher::FetchRequest& request )
{
    const auto cacheKeyOpt = std::visit(
        qwr::Visitor{
            []( const drp::ArtworkFetcher::MusicBrainzFetchRequest& req ) {
                return drp::artwork::BuildMusicBrainzCacheKey( req.artist, req.album, req.userReleaseMbidOpt );
            },
            []( const drp::ArtworkFetcher::LocalArtworkUploadRequest& req ) {
                return req.host == drp::artwork::LocalArtworkHost::Catbox
                           ? drp::artwork::BuildCatboxCacheKey( req.artPinId, req.options.maxWidth, req.options.maxHeight )
                           : drp::artwork::BuildImgurCacheKey( req.artPinId, req.options.maxWidth, req.options.maxHeight );
            },
            []( const drp::ArtworkFetcher::TheAudioDbFetchRequest& req ) {
                return drp::artwork::BuildTheAudioDbCacheKey( req.artist, req.album );
            } },
        request );
    if ( !cacheKeyOpt )
    {
        return std::nullopt;
    }
    if ( cacheKeyOpt->size() > kMaxCacheKeyBytes )
    {
        drp::LogWarning( "Skipping album art request because the art cache key is too large" );
        return std::nullopt;
    }

    return cacheKeyOpt;
}

int64_t CurrentUnixTime()
{
    const auto now = std::time( nullptr );
    return now < 0 ? 0 : static_cast<int64_t>( now );
}

bool IsRequestExecutable( const drp::ArtworkFetcher::FetchRequest& request )
{
    return std::visit(
        qwr::Visitor{
            []( const drp::ArtworkFetcher::MusicBrainzFetchRequest& req ) {
                return true;
            },
            []( const drp::ArtworkFetcher::LocalArtworkUploadRequest& req ) {
                return drp::artwork::AreValidLocalArtworkUploadOptions( req.options )
                       && ( req.host == drp::artwork::LocalArtworkHost::Catbox || !req.imgurClientId.empty() );
            },
            []( const drp::ArtworkFetcher::TheAudioDbFetchRequest& req ) {
                return drp::artwork::IsEligibleTheAudioDbSupporterKey( req.apiKey );
            } },
        request );
}

qwr::u8string_view ProviderName( const drp::ArtworkFetcher::FetchRequest& request )
{
    return std::visit(
        qwr::Visitor{
            []( const drp::ArtworkFetcher::MusicBrainzFetchRequest& ) -> qwr::u8string_view {
                return "MusicBrainz / Cover Art Archive";
            },
            []( const drp::ArtworkFetcher::LocalArtworkUploadRequest& req ) -> qwr::u8string_view {
                return req.host == drp::artwork::LocalArtworkHost::Catbox ? "Catbox" : "Imgur";
            },
            []( const drp::ArtworkFetcher::TheAudioDbFetchRequest& ) -> qwr::u8string_view {
                return "TheAudioDB";
            } },
        request );
}

qwr::u8string FetchingMessage( const drp::ArtworkFetcher::FetchRequest& request )
{
    if ( std::holds_alternative<drp::ArtworkFetcher::LocalArtworkUploadRequest>( request ) )
    {
        return fmt::format( "Uploading local artwork to {}...", ProviderName( request ) );
    }
    return fmt::format( "Fetching album artwork from {}...", ProviderName( request ) );
}

qwr::u8string ResolvedMessage( const drp::ArtworkFetcher::FetchRequest& request, bool cached )
{
    if ( cached )
    {
        return fmt::format( "Artwork resolved from the {} cache.", ProviderName( request ) );
    }
    return fmt::format( "Artwork resolved using {}.", ProviderName( request ) );
}

qwr::u8string NotFoundMessage( const drp::ArtworkFetcher::FetchRequest& request, bool cached )
{
    if ( cached )
    {
        return "No artwork was found (cached result).";
    }
    return fmt::format( "No artwork was found by {}.", ProviderName( request ) );
}

bool IsDiscordImageKeyInvalid( const qwr::u8string& imageKey )
{
    try
    {
        qwr::unicode::ToWide( imageKey );
        if ( drp::validation::IsSecureImageUrl( imageKey ) )
        {
            return false;
        }

        drp::LogError( "Failed to process art URL: expected an HTTPS URL without whitespace, at most 254 bytes long" );
        return true;
    }
    catch ( const std::exception& e )
    {
        drp::LogError( fmt::format( "Failed to process art URL: invalid UTF-8 ({})", e.what() ) );
        return true;
    }
}

} // namespace

namespace drp
{

drp::ArtworkFetcher& ArtworkFetcher::Get()
{
    static ArtworkFetcher instance;
    return instance;
}

void ArtworkFetcher::Initialize()
{
    LoadCache();

    {
        std::unique_lock lock( mutex_ );
        isWorkerAvailable_ = true;
    }

    try
    {
        StartThread();
    }
    catch ( const std::exception& e )
    {
        StopThread();
        SetWorkerFailure( fmt::format( "Failed to start artwork worker: {}", e.what() ) );
    }
    catch ( ... )
    {
        StopThread();
        SetWorkerFailure( "Failed to start artwork worker" );
    }
}

void ArtworkFetcher::Finalize()
{
    StopThread();
    SaveCache();
}

std::optional<qwr::u8string> ArtworkFetcher::GetArtUrl( const std::vector<FetchRequest>& requests )
{
    if ( requests.empty() )
    {
        std::unique_lock lock( mutex_ );
        SupersedeCurrentRequestLocked();
        SetStatusLocked( Status::NotFound, "No enabled artwork provider is ready to run." );
        lock.unlock();
        cv_.notify_all();
        return std::nullopt;
    }

    std::unique_lock lock( mutex_ );
    const auto now = CurrentUnixTime();
    bool sawIncompleteMetadata = false;
    bool sawInvalidConfiguration = false;
    bool sawCachedNoMatch = false;
    bool sawProviderFailure = false;
    bool sawRejectedCredential = false;
    std::optional<std::pair<FetchRequest, qwr::u8string>> pendingCandidate;
    std::optional<std::pair<FetchRequest, qwr::u8string>> cachedFallback;

    for ( const auto& request: requests )
    {
        const auto cacheKeyOpt = GenerateCacheKey( request );
        if ( !cacheKeyOpt )
        {
            sawIncompleteMetadata = true;
            continue;
        }

        auto cacheIt = cacheKeyToEntry_.find( *cacheKeyOpt );
        if ( cacheIt != cacheKeyToEntry_.end() )
        {
            const auto lifetime = cacheIt->second.artUrl ? kPositiveCacheLifetime : kNegativeCacheLifetime;
            if ( drp::validation::IsCacheEntryFresh( cacheIt->second.fetchedAt, now, lifetime.count() ) )
            {
                if ( cacheIt->second.artUrl )
                {
                    if ( pendingCandidate )
                    {
                        cachedFallback = std::pair{ request, *cacheIt->second.artUrl };
                        break;
                    }

                    SetStatusLocked( Status::Resolved, ResolvedMessage( request, true ) );
                    const auto artUrl = cacheIt->second.artUrl;
                    SupersedeCurrentRequestLocked();
                    lock.unlock();
                    cv_.notify_all();
                    return artUrl;
                }
                sawCachedNoMatch = true;
                continue;
            }
            cacheKeyToEntry_.erase( cacheIt );
        }

        if ( std::holds_alternative<TheAudioDbFetchRequest>( request ) )
        {
            const auto& theAudioDbRequest = std::get<TheAudioDbFetchRequest>( request );
            if ( theaudiodb::IsApiKeyRejected( theAudioDbRequest.apiKey ) )
            {
                sawRejectedCredential = true;
                continue;
            }
            if ( theaudiodb::IsRateLimited( theAudioDbRequest.apiKey ) )
            {
                sawProviderFailure = true;
                continue;
            }
        }

        auto failureIt = cacheKeyToRetryAfter_.find( *cacheKeyOpt );
        if ( failureIt != cacheKeyToRetryAfter_.end() )
        {
            if ( failureIt->second > now )
            {
                sawProviderFailure = true;
                continue;
            }
            cacheKeyToRetryAfter_.erase( failureIt );
        }

        if ( !IsRequestExecutable( request ) )
        {
            sawInvalidConfiguration = true;
            continue;
        }

        if ( !pendingCandidate )
        {
            pendingCandidate = std::pair{ request, *cacheKeyOpt };
        }
    }

    if ( pendingCandidate )
    {
        const auto& [request, cacheKey] = *pendingCandidate;
        if ( isWorkerAvailable_ )
        {
            if ( !currentRequestOpt_ || currentRequestOpt_->request != request )
            {
                ++requestGeneration_;
                currentRequestOpt_ = PendingRequest{ request, cacheKey, requestGeneration_ };
                cv_.notify_all();
            }

            if ( cachedFallback )
            {
                SetStatusLocked(
                    Status::Fetching,
                    fmt::format(
                        "{} Using cached artwork from {} while waiting.",
                        FetchingMessage( request ),
                        ProviderName( cachedFallback->first ) ) );
                return cachedFallback->second;
            }

            SetStatusLocked( Status::Fetching, FetchingMessage( request ) );
            return std::nullopt;
        }

        SupersedeCurrentRequestLocked();
        if ( cachedFallback )
        {
            SetStatusLocked(
                Status::Resolved,
                fmt::format(
                    "Artwork worker is unavailable; using cached artwork from {}.",
                    ProviderName( cachedFallback->first ) ) );
            return cachedFallback->second;
        }

        SetStatusLocked( Status::Failed, "Artwork worker is unavailable; the configured fallback will be used." );
        return std::nullopt;
    }

    SupersedeCurrentRequestLocked();
    if ( sawRejectedCredential )
    {
        SetStatusLocked( Status::Failed, "TheAudioDB rejected the stored API key; retest it or store a replacement key." );
    }
    else if ( sawProviderFailure )
    {
        SetStatusLocked( Status::Failed, "No artwork was resolved; an enabled provider recently failed." );
    }
    else if ( sawInvalidConfiguration )
    {
        SetStatusLocked( Status::Failed, "No artwork was resolved; an enabled provider is not configured correctly." );
    }
    else if ( sawCachedNoMatch )
    {
        SetStatusLocked( Status::NotFound, "No artwork was found by the enabled providers (cached result)." );
    }
    else if ( sawIncompleteMetadata )
    {
        SetStatusLocked( Status::NotFound, "Artwork cannot be requested because the current track metadata is incomplete." );
    }
    else
    {
        SetStatusLocked( Status::NotFound, "No artwork was found by the enabled providers." );
    }
    lock.unlock();
    cv_.notify_all();
    return std::nullopt;
}

ArtworkFetcher::StatusSnapshot ArtworkFetcher::GetStatus() const
{
    std::unique_lock lock( mutex_ );
    return status_;
}

void ArtworkFetcher::CancelPendingRequest()
{
    std::unique_lock lock( mutex_ );
    if ( !currentRequestOpt_ )
    {
        return;
    }

    SupersedeCurrentRequestLocked();
    SetStatusLocked( Status::Idle, "Artwork request cancelled." );
    lock.unlock();
    cv_.notify_all();
}

bool ArtworkFetcher::LoadCache( bool throwOnError )
{
    using json = nlohmann::json;

    try
    {
        const auto cachePath = GetCacheFilePath();
        if ( !fs::exists( cachePath ) )
        {
            return false;
        }
        if ( fs::file_size( cachePath ) > kMaxCacheFileBytes )
        {
            throw qwr::QwrException( "Art cache is too large to load safely: {}", cachePath.u8string() );
        }

        const auto content = qwr::file::ReadFile( cachePath, CP_UTF8 );

        decltype( cacheKeyToEntry_ ) loadedCache;
        const auto root = json::parse( content );
        if ( !root.is_object() || root.at( "version" ).get<int>() != artwork::kArtworkCacheFormatVersion )
        {
            throw qwr::QwrException( "Unsupported art cache format: {}", cachePath.u8string() );
        }
        const auto& entries = root.at( "entries" );
        if ( !entries.is_object() )
        {
            throw qwr::QwrException( "Invalid art cache entries: {}", cachePath.u8string() );
        }

        const auto now = CurrentUnixTime();
        for ( const auto& [key, value]: entries.items() )
        {
            if ( key.size() > kMaxCacheKeyBytes || !artwork::IsQualifiedCacheKey( key ) || loadedCache.size() >= kMaxCacheEntries || !value.is_object() )
            {
                continue;
            }

            CacheEntry entry{
                value.at( "url" ).get<std::optional<qwr::u8string>>(),
                value.at( "fetched_at" ).get<int64_t>() };
            if ( entry.artUrl && !validation::IsSecureImageUrl( *entry.artUrl ) )
            {
                continue;
            }

            const auto lifetime = entry.artUrl ? kPositiveCacheLifetime : kNegativeCacheLifetime;
            if ( !validation::IsCacheEntryFresh( entry.fetchedAt, now, lifetime.count() ) )
            {
                continue;
            }

            loadedCache.emplace( key, std::move( entry ) );
        }

        {
            std::unique_lock lock( mutex_ );
            cacheKeyToEntry_ = std::move( loadedCache );
            cacheKeyToRetryAfter_.clear();
            ++cacheGeneration_;
            SupersedeCurrentRequestLocked();
            SetStatusLocked( Status::Idle, "Artwork cache loaded; the active track will be re-evaluated where applicable." );
        }
        cv_.notify_all();
        return true;
    }
    catch ( const std::exception& e )
    {
        LogError( fmt::format( "Failed to load cache: {}", e.what() ) );
        if ( throwOnError )
        {
            throw;
        }
    }

    return false;
}

void ArtworkFetcher::SaveCache()
{
    using json = nlohmann::json;

    try
    {
        const auto cachePath = GetCacheFilePath();
        fs::create_directories( cachePath.parent_path() );

        decltype( cacheKeyToEntry_ ) cacheCopy;
        {
            std::unique_lock lock( mutex_ );
            cacheCopy = cacheKeyToEntry_;
        }

        json entries = json::object();
        for ( const auto& [key, entry]: cacheCopy )
        {
            entries[key] = json{ { "url", entry.artUrl }, { "fetched_at", entry.fetchedAt } };
        }
        const auto content = json{ { "version", artwork::kArtworkCacheFormatVersion }, { "entries", std::move( entries ) } }.dump( 2 );
        const auto temporaryPath = fs::u8path( cachePath.u8string() + ".tmp" );
        qwr::file::WriteFile( temporaryPath, content, false );
        if ( !MoveFileExW( temporaryPath.c_str(), cachePath.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH ) )
        {
            fs::remove( temporaryPath );
            throw fs::filesystem_error( "Failed to atomically replace art cache", cachePath, std::error_code( GetLastError(), std::system_category() ) );
        }
    }
    catch ( const qwr::QwrException& e )
    {
        LogError( fmt::format( "Failed to save cache: {}", e.what() ) );
    }
    catch ( const json::exception& e )
    {
        LogError( fmt::format( "Failed to save cache: {}", e.what() ) );
    }
    catch ( const fs::filesystem_error& e )
    {
        LogError( fmt::format( "Failed to save cache: {}", e.what() ) );
    }
    catch ( const std::exception& e )
    {
        LogError( fmt::format( "Failed to save cache: {}", e.what() ) );
    }
}

void ArtworkFetcher::ClearCache()
{
    try
    {
        fs::remove( drp::path::ImageDir() / "art_urls.v2.0.1.json" );
        fs::remove( drp::path::ImageDir() / "art_urls.v3.json" );
        fs::remove( drp::path::ImageDir() / "art_urls.v4.json" );
        fs::remove( drp::path::ImageDir() / "art_urls.v5.json" );
        fs::remove( GetCacheFilePath() );
    }
    catch ( const fs::filesystem_error& e )
    {
        LogError( fmt::format( "Failed to clear cache: {}", e.what() ) );
        throw;
    }

    {
        std::unique_lock lock( mutex_ );
        cacheKeyToEntry_.clear();
        cacheKeyToRetryAfter_.clear();
        ++cacheGeneration_;
        SupersedeCurrentRequestLocked();
        SetStatusLocked( Status::Idle, "Artwork cache cleared; no artwork requested yet." );
    }
    cv_.notify_all();
}

void ArtworkFetcher::InvalidateProviderCache( ProviderCache provider )
{
    const qwr::u8string_view prefix = provider == ProviderCache::Catbox ? "catbox:"
                                         : provider == ProviderCache::Imgur ? "imgur:"
                                                                           : "tadb:";
    bool removedCacheEntry = false;
    {
        std::unique_lock lock( mutex_ );
        removedCacheEntry = std::erase_if( cacheKeyToEntry_, [prefix]( const auto& item ) {
            return qwr::u8string_view{ item.first }.starts_with( prefix );
        } ) > 0;
        std::erase_if( cacheKeyToRetryAfter_, [prefix]( const auto& item ) {
            return qwr::u8string_view{ item.first }.starts_with( prefix );
        } );
        ++cacheGeneration_;
        SupersedeCurrentRequestLocked();
        SetStatusLocked( Status::Idle, "Provider settings changed; the active track will be re-evaluated." );
    }
    cv_.notify_all();
    if ( removedCacheEntry )
    {
        SaveCache();
    }
}

std::filesystem::path ArtworkFetcher::GetCacheFilePath()
{
    static const auto cachePath = drp::path::ImageDir() / "art_urls.v6.json";
    return cachePath;
}

void ArtworkFetcher::StartThread()
{
    pThread_ = std::make_unique<std::jthread>( [this]( std::stop_token token ) {
        try
        {
            ThreadMain( token );
        }
        catch ( const std::exception& e )
        {
            SetWorkerFailure( fmt::format( "Artwork worker stopped unexpectedly: {}", e.what() ) );
        }
        catch ( ... )
        {
            SetWorkerFailure( "Artwork worker stopped unexpectedly" );
        }
    } );
    qwr::SetThreadName( *pThread_, "DRP ArtFetcher" );
}

void ArtworkFetcher::StopThread()
{
    {
        std::unique_lock lock( mutex_ );
        isWorkerAvailable_ = false;
        SupersedeCurrentRequestLocked();
    }

    if ( pThread_ )
    {
        pThread_->request_stop();
        cv_.notify_all();
        pThread_.reset();
    }
}

void ArtworkFetcher::ThreadMain( std::stop_token token )
{
    std::optional<PendingRequest> lastRequest;

    while ( !token.stop_requested() )
    {
        std::optional<WorkItem> workOpt;
        {
            std::unique_lock lock( mutex_ );
            cv_.wait_for( lock, kRequestProcessingDelay, [&] {
                return token.stop_requested() || lastRequest != currentRequestOpt_;
            } );

            if ( lastRequest != currentRequestOpt_ )
            { // user requested new art, wait again
                lastRequest = currentRequestOpt_;
                continue;
            }

            if ( !lastRequest )
            {
                continue;
            }

            workOpt = WorkItem{ *lastRequest, cacheGeneration_ };
        }

        auto& work = *workOpt;
        auto outcome = std::visit(
            [&]( const auto& arg ) {
                using RequestType = std::decay_t<decltype( arg )>;
                if constexpr ( std::is_same_v<RequestType, TheAudioDbFetchRequest> )
                {
                    return ProcessFetchRequest( arg, work.pending.requestGeneration, work.cacheGeneration );
                }
                else
                {
                    return ProcessFetchRequest( arg );
                }
            },
            work.pending.request );
        if ( !outcome.artUrl && ( qwr::GlobalAbortCallback::GetInstance().is_aborting() || token.stop_requested() ) )
        { // do not save nullopt if interrupted, because it might actually had the image
            return;
        }

        if ( outcome.artUrl && IsDiscordImageKeyInvalid( *outcome.artUrl ) )
        {
            outcome.artUrl.reset();
            outcome.cacheable = false;
            outcome.failureMessage = "An artwork provider returned an image URL Discord cannot use; trying the next provider.";
        }

        bool shouldRefreshImage = outcome.providerSuppressed;
        {
            std::unique_lock lock( mutex_ );
            const bool mayCommit = artwork::CanCommitCacheResult( work.cacheGeneration, cacheGeneration_ );
            const auto currentGenerationOpt = currentRequestOpt_
                                                  ? std::optional<uint64_t>{ currentRequestOpt_->requestGeneration }
                                                  : std::nullopt;
            const bool matchesCurrentGeneration = artwork::CanPublishRequestResult( work.pending.requestGeneration, currentGenerationOpt );
            const bool satisfiesCurrentCacheKey = mayCommit
                                                  && outcome.cacheable
                                                  && currentRequestOpt_
                                                  && currentRequestOpt_->cacheKey == work.pending.cacheKey;
            const bool mayPublish = mayCommit && ( matchesCurrentGeneration || satisfiesCurrentCacheKey );

            if ( mayCommit && outcome.cacheable )
            {
                cacheKeyToRetryAfter_.erase( work.pending.cacheKey );
                if ( !cacheKeyToEntry_.contains( work.pending.cacheKey ) && cacheKeyToEntry_.size() >= kMaxCacheEntries )
                {
                    const auto oldest = std::min_element( cacheKeyToEntry_.begin(), cacheKeyToEntry_.end(), []( const auto& left, const auto& right ) {
                        return left.second.fetchedAt < right.second.fetchedAt;
                    } );
                    if ( oldest != cacheKeyToEntry_.end() )
                    {
                        cacheKeyToEntry_.erase( oldest );
                    }
                }
                cacheKeyToEntry_.insert_or_assign( work.pending.cacheKey, CacheEntry{ outcome.artUrl, CurrentUnixTime() } );
            }
            else if ( mayCommit && !outcome.failureMessage.empty() )
            {
                if ( !outcome.providerSuppressed )
                {
                    if ( !cacheKeyToRetryAfter_.contains( work.pending.cacheKey ) && cacheKeyToRetryAfter_.size() >= kMaxCacheEntries )
                    {
                        const auto earliestRetry = std::min_element( cacheKeyToRetryAfter_.begin(), cacheKeyToRetryAfter_.end(), []( const auto& left, const auto& right ) {
                            return left.second < right.second;
                        } );
                        if ( earliestRetry != cacheKeyToRetryAfter_.end() )
                        {
                            cacheKeyToRetryAfter_.erase( earliestRetry );
                        }
                    }
                    cacheKeyToRetryAfter_.insert_or_assign( work.pending.cacheKey, CurrentUnixTime() + kProviderFailureCooldown.count() );
                }
            }

            if ( mayPublish )
            {
                const auto& statusRequest = satisfiesCurrentCacheKey ? currentRequestOpt_->request : work.pending.request;

                if ( outcome.artUrl )
                {
                    SetStatusLocked( Status::Resolved, ResolvedMessage( statusRequest, false ) );
                }
                else if ( outcome.cacheable )
                {
                    SetStatusLocked( Status::NotFound, NotFoundMessage( statusRequest, false ) );
                }
                else
                {
                    SetStatusLocked( Status::Failed, outcome.failureMessage.empty() ? "Artwork request failed; see the foobar2000 console." : outcome.failureMessage );
                }

                shouldRefreshImage = outcome.cacheable || !outcome.failureMessage.empty();
                currentRequestOpt_.reset();
            }
            lastRequest.reset();
        }

        if ( shouldRefreshImage && !token.stop_requested() && !qwr::GlobalAbortCallback::GetInstance().is_aborting() )
        {
            fb2k::inMainThread( [] {
                if ( qwr::GlobalAbortCallback::GetInstance().is_aborting() )
                {
                    return;
                }

                auto pm = DiscordAdapter::GetInstance().GetPresenceModifier();
                pm.UpdateImage();
            } );
        }
    }
}

ArtworkFetcher::FetchOutcome ArtworkFetcher::ProcessFetchRequest( const MusicBrainzFetchRequest& request )
{
    try
    {
        return { musicbrainz::FetchArt( request.artist, request.album, request.userReleaseMbidOpt ), true };
    }
    catch ( const qwr::QwrException& e )
    {
        LogError( e.what() );
        return { {}, false, "MusicBrainz artwork request failed; see the foobar2000 console." };
    }
    catch ( const exception_aborted& /*e*/ )
    {
        return {};
    }
    catch ( const std::exception& e )
    {
        LogError( fmt::format( "Unexpected MusicBrainz art fetch failure: {}", e.what() ) );
        return { {}, false, "MusicBrainz artwork request failed; see the foobar2000 console." };
    }
    catch ( ... )
    {
        LogError( "Unexpected MusicBrainz art fetch failure" );
        return { {}, false, "MusicBrainz artwork request failed; see the foobar2000 console." };
    }
}

ArtworkFetcher::FetchOutcome ArtworkFetcher::ProcessFetchRequest( const LocalArtworkUploadRequest& request )
{
    try
    {
        return { artwork::UploadLocalArtwork(
                     request.handle,
                     request.host,
                     request.imgurClientId,
                     request.options,
                     qwr::GlobalAbortCallback::GetInstance() ),
            true };
    }
    catch ( const qwr::QwrException& e )
    {
        LogError( e.what() );
        return { {}, false, "Local artwork upload failed; see the foobar2000 console." };
    }
    catch ( const exception_aborted& /*e*/ )
    {
        return {};
    }
    catch ( const pfc::exception& e )
    {
        LogError( fmt::format( "Local artwork upload failed: {}", e.what() ) );
        return { {}, false, "Local artwork upload failed; see the foobar2000 console." };
    }
    catch ( ... )
    {
        LogError( "Unexpected local artwork upload failure" );
        return { {}, false, "Local artwork upload failed; see the foobar2000 console." };
    }
}

ArtworkFetcher::FetchOutcome ArtworkFetcher::ProcessFetchRequest(
    const TheAudioDbFetchRequest& request,
    uint64_t requestGeneration,
    uint64_t cacheGeneration )
{
    try
    {
        auto& aborter = qwr::GlobalAbortCallback::GetInstance();
        return {
            theaudiodb::FetchArt(
                request.artist,
                request.album,
                request.apiKey,
                aborter,
                [this, requestGeneration, cacheGeneration] {
                    std::unique_lock lock( mutex_ );
                    return cacheGeneration == cacheGeneration_
                           && currentRequestOpt_
                           && currentRequestOpt_->requestGeneration == requestGeneration;
                } ),
            true };
    }
    catch ( const theaudiodb::RateLimitedException& e )
    {
        LogWarning( e.what() );
        return { {}, false, "TheAudioDB rate limit reached; the provider will be skipped for one minute.", true };
    }
    catch ( const theaudiodb::AuthenticationRejectedException& e )
    {
        LogError( e.what() );
        return { {}, false, "TheAudioDB rejected the stored API key; retest it or store a replacement key.", true };
    }
    catch ( const qwr::QwrException& e )
    {
        LogError( e.what() );
        return { {}, false, "TheAudioDB artwork request failed; see the foobar2000 console." };
    }
    catch ( const exception_aborted& /*e*/ )
    {
        return {};
    }
    catch ( const std::exception& e )
    {
        LogError( fmt::format( "Unexpected TheAudioDB art fetch failure: {}", e.what() ) );
        return { {}, false, "TheAudioDB artwork request failed; see the foobar2000 console." };
    }
    catch ( ... )
    {
        LogError( "Unexpected TheAudioDB art fetch failure" );
        return { {}, false, "TheAudioDB artwork request failed; see the foobar2000 console." };
    }
}

void ArtworkFetcher::SupersedeCurrentRequestLocked()
{
    ++requestGeneration_;
    currentRequestOpt_.reset();
}

void ArtworkFetcher::SetWorkerFailure( qwr::u8string logMessage )
{
    LogError( logMessage );
    {
        std::unique_lock lock( mutex_ );
        isWorkerAvailable_ = false;
        SupersedeCurrentRequestLocked();
        SetStatusLocked( Status::Failed, "Artwork worker is unavailable; the configured fallback will be used." );
    }
    cv_.notify_all();
}

void ArtworkFetcher::SetStatusLocked( Status status, qwr::u8string message )
{
    status_ = StatusSnapshot{ status, std::move( message ) };
}

} // namespace drp
