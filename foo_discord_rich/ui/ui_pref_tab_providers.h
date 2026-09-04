#pragma once

#include <fb2k/config.h>
#include <artwork/local_artwork_uploader.h>
#include <ui/ui_itab.h>

#include <resource.h>

#include <foobar2000/SDK/coreDarkMode.h>
#include <qwr/fb2k_config_ui_option.h>
#include <qwr/macros.h>
#include <qwr/ui_ddx_option.h>

#include <array>
#include <atomic>
#include <memory>
#include <optional>

namespace drp::ui
{

class PreferenceTabManager;

class PreferenceTabProviders
    : public CDialogImpl<PreferenceTabProviders>
    , public CWinDataExchange<PreferenceTabProviders>
    , public ITab
{
public:
    static constexpr UINT kTheAudioDbTestFinishedMessage = WM_APP + 0x152;

    enum
    {
        IDD = IDD_PREFS_PROVIDERS_TAB
    };

    BEGIN_MSG_MAP( PreferenceTabProviders )
        MSG_WM_INITDIALOG( OnInitDialog )
        MSG_WM_DESTROY( OnDestroy )
        COMMAND_HANDLER_EX( IDC_CHECK_FETCH_ALBUM_ART, BN_CLICKED, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_CHECK_FETCH_THEAUDIODB, BN_CLICKED, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_EDIT_THEAUDIODB_API_KEY, EN_CHANGE, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_CHECK_SHOW_THEAUDIODB_KEY, BN_CLICKED, OnShowApiKeyClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_TEST_THEAUDIODB, BN_CLICKED, OnTestTheAudioDbClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_CLEAR_THEAUDIODB_KEY, BN_CLICKED, OnClearTheAudioDbKeyClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_THEAUDIODB_HELP, BN_CLICKED, OnTheAudioDbHelpClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_MUSICBRAINZ_HELP, BN_CLICKED, OnMusicBrainzHelpClick )
        COMMAND_HANDLER_EX( IDC_CHECK_UPLOAD_CATBOX, BN_CLICKED, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_CHECK_UPLOAD_IMGUR, BN_CLICKED, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_EDIT_IMGUR_CLIENT_ID, EN_CHANGE, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_EDIT_LOCAL_ART_PIN_QUERY, EN_CHANGE, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_EDIT_LOCAL_ART_MAX_WIDTH, EN_CHANGE, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_EDIT_LOCAL_ART_MAX_HEIGHT, EN_CHANGE, OnDdxUiChange )
        COMMAND_HANDLER_EX( IDC_BUTTON_TEST_CATBOX, BN_CLICKED, OnTestCatboxClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_TEST_IMGUR, BN_CLICKED, OnTestImgurClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_IMGUR_HELP, BN_CLICKED, OnImgurHelpClick )
        COMMAND_HANDLER_EX( IDC_BUTTON_PROVIDER_REQUIREMENTS, BN_CLICKED, OnRequirementsClick )
        MESSAGE_HANDLER( kTheAudioDbTestFinishedMessage, OnTheAudioDbTestFinished )
    END_MSG_MAP()

public:
    explicit PreferenceTabProviders( PreferenceTabManager* pParent );
    ~PreferenceTabProviders() override;

    HWND CreateTab( HWND hParent ) override;
    CDialogImplBase& Dialog() override;
    const wchar_t* Name() const override;
    void OnUiChangeRequest( int nID, bool enable ) override;
    t_uint32 GetState() override;
    void Apply() override;
    void Reset() override;

    bool HasPendingArtworkSettings() const;

private:
    struct TheAudioDbTestState
    {
        std::atomic_bool isRunning = false;
        std::atomic<HWND> dialogHwnd = nullptr;
    };

    BOOL OnInitDialog( HWND hwndFocus, LPARAM lParam );
    void OnDestroy();
    void OnDdxUiChange( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnShowApiKeyClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnTestTheAudioDbClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnClearTheAudioDbKeyClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnTheAudioDbHelpClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnMusicBrainzHelpClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnTestCatboxClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnTestImgurClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnImgurHelpClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    void OnRequirementsClick( UINT uNotifyCode, int nID, CWindow wndCtl );
    LRESULT OnTheAudioDbTestFinished( UINT message, WPARAM wParam, LPARAM lParam, BOOL& wasHandled );
    void OnChanged();
    void DoFullDdxToUi();
    void UpdateControlState();
    std::optional<qwr::u8string> GetEffectiveTheAudioDbApiKey();
    void ClearPendingTheAudioDbApiKey();
    void CancelPendingTheAudioDbCredentialChange();
    void UpdateTheAudioDbCredentialUi();
    void TestLocalArtworkUpload( artwork::LocalArtworkHost host, qwr::u8string_view imgurClientId );

private:
    PreferenceTabManager* pParent_ = nullptr;

#define SPTF_DEFINE_UI_OPTION( name ) \
    qwr::ui::UiOption<decltype( config::name )> name##_;

#define SPTF_DEFINE_UI_OPTIONS( ... ) \
    QWR_EXPAND( QWR_PASTE( SPTF_DEFINE_UI_OPTION, __VA_ARGS__ ) )

    SPTF_DEFINE_UI_OPTIONS( enableAlbumArtFetch,
                            enableTheAudioDbFetch,
                            enableCatboxUpload,
                            enableImgurUpload,
                            imgurClientId,
                            localArtworkPinQuery,
                            localArtworkUploadMaxWidth,
                            localArtworkUploadMaxHeight )

#undef SPTF_DEFINE_UI_OPTIONS
#undef SPTF_DEFINE_UI_OPTION

    std::array<std::unique_ptr<qwr::ui::IUiDdxOption>, 8> ddxOptions_;

    qwr::u8string pendingTheAudioDbApiKey_;
    bool hasStoredTheAudioDbApiKey_ = false;
    bool isTheAudioDbKeyDeletionPending_ = false;
    bool isRestoringTheAudioDbCredentialUi_ = false;
    std::shared_ptr<TheAudioDbTestState> theAudioDbTestState_ = std::make_shared<TheAudioDbTestState>();
    fb2k::CCoreDarkModeHooks darkModeHooks_;
};

} // namespace drp::ui
