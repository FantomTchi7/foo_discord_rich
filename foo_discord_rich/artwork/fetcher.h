#pragma once

#include <artwork/local_artwork_uploader.h>

#include <cstdint>
#include <mutex>
#include <optional>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

namespace drp
{

class ArtworkFetcher
{
public:
    enum class ProviderCache
    {
        Catbox,
        Imgur,
        TheAudioDb
    };

    enum class Status
    {
        Idle,
        Fetching,
        Resolved,
        NotFound,
        Failed
    };

    struct StatusSnapshot
    {
        Status status = Status::Idle;
        qwr::u8string message = "No artwork requested yet.";
    };

    struct MusicBrainzFetchRequest
    {
        qwr::u8string artist;
        qwr::u8string album;
        std::optional<qwr::u8string> userReleaseMbidOpt;

        auto operator<=>( const MusicBrainzFetchRequest& other ) const = default;
    };

    struct LocalArtworkUploadRequest
    {
        qwr::u8string artPinId;
        metadb_handle_ptr handle;
        artwork::LocalArtworkHost host;
        qwr::u8string imgurClientId;
        artwork::LocalArtworkUploadOptions options;

        auto operator<=>( const LocalArtworkUploadRequest& other ) const = default;
    };

    struct TheAudioDbFetchRequest
    {
        qwr::u8string artist;
        qwr::u8string album;
        qwr::u8string apiKey;

        auto operator<=>( const TheAudioDbFetchRequest& other ) const = default;
    };

    using FetchRequest = std::variant<MusicBrainzFetchRequest, LocalArtworkUploadRequest, TheAudioDbFetchRequest>;

public:
    static ArtworkFetcher& Get();

    void Initialize();
    void Finalize();

    std::optional<qwr::u8string> GetArtUrl( const std::vector<FetchRequest>& requests );
    StatusSnapshot GetStatus() const;
    void CancelPendingRequest();

    bool LoadCache( bool throwOnError = false );
    void SaveCache();
    void ClearCache();
    void InvalidateProviderCache( ProviderCache provider );
    static std::filesystem::path GetCacheFilePath();

private:
    struct FetchOutcome
    {
        std::optional<qwr::u8string> artUrl;
        bool cacheable = false;
        qwr::u8string failureMessage;
        bool providerSuppressed = false;
    };

    struct CacheEntry
    {
        std::optional<qwr::u8string> artUrl;
        int64_t fetchedAt = 0;
    };

    struct PendingRequest
    {
        FetchRequest request;
        qwr::u8string cacheKey;
        uint64_t requestGeneration = 0;

        bool operator==( const PendingRequest& other ) const = default;
    };

    struct WorkItem
    {
        PendingRequest pending;
        uint64_t cacheGeneration = 0;
    };

    void StartThread();
    void StopThread();

    void ThreadMain( std::stop_token token );

    FetchOutcome ProcessFetchRequest( const MusicBrainzFetchRequest& request );
    FetchOutcome ProcessFetchRequest( const LocalArtworkUploadRequest& request );
    FetchOutcome ProcessFetchRequest( const TheAudioDbFetchRequest& request, uint64_t requestGeneration, uint64_t cacheGeneration );
    void SupersedeCurrentRequestLocked();
    void SetWorkerFailure( qwr::u8string logMessage );
    void SetStatusLocked( Status status, qwr::u8string message );

private:
    ArtworkFetcher() = default;

private:
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::unique_ptr<std::jthread> pThread_;

    std::optional<PendingRequest> currentRequestOpt_;
    std::unordered_map<qwr::u8string, CacheEntry> cacheKeyToEntry_;
    std::unordered_map<qwr::u8string, int64_t> cacheKeyToRetryAfter_;
    uint64_t requestGeneration_ = 0;
    uint64_t cacheGeneration_ = 0;
    bool isWorkerAvailable_ = false;
    StatusSnapshot status_;
};

} // namespace drp
