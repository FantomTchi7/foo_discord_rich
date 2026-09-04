#include <stdafx.h>

#include "ui_pref_tab_main.h"

#include <artwork/fetcher.h>
#include <discord/discord_integration.h>
#include <ui/ui_pref_tab_manager.h>

#include <qwr/fb2k_config_ui_option.h>

namespace drp::ui
{

namespace
{
constexpr UINT_PTR kArtworkStatusTimerId = 1;

qwr::u8string EvaluatePreviewLine( const qwr::u8string& query )
{
    titleformat_object::ptr format;
    titleformat_compiler::get()->compile_safe( format, query.c_str() );
    if ( format.is_empty() )
    {
        return "<invalid title format>";
    }
    pfc::string8_fast result;
    metadb_handle_ptr ignored;
    playback_control::get()->playback_format_title_ex( ignored, nullptr, result, format, nullptr, playback_control::display_level_all );
    return result.is_empty() ? "<empty>" : result.c_str();
}
} // namespace

using namespace config;

PreferenceTabMain::PreferenceTabMain( PreferenceTabManager* pParent )
    : pParent_( pParent )
    , isEnabled_( config::isEnabled )
    , topTextQuery_( config::topTextQuery )
    , middleTextQuery_( config::middleTextQuery )
    , bottomTextQuery_( config::bottomTextQuery )
    , artworkDisplayPolicy_( config::artworkDisplayPolicy,
          { { artwork::DisplayPolicy::PreferArtwork, 0 }, { artwork::DisplayPolicy::ApplicationIcon, 1 }, { artwork::DisplayPolicy::ArtworkOnly, 2 } } )
    , largeImageSettings_( config::largeImageSettings, { { ImageSetting::Light, IDC_RADIO_IMG_LIGHT }, { ImageSetting::Dark, IDC_RADIO_IMG_DARK }, { ImageSetting::Disabled, IDC_RADIO_IMG_DISABLED } } )
    , smallImageSettings_( config::smallImageSettings, { { ImageSetting::Light, IDC_RADIO_PLAYBACK_IMG_LIGHT }, { ImageSetting::Dark, IDC_RADIO_PLAYBACK_IMG_DARK }, { ImageSetting::Disabled, IDC_RADIO_PLAYBACK_IMG_DISABLED } } )
    , disableWhenPaused_( config::disableWhenPaused )
    , swapSmallImages_( config::swapSmallImages )
    , ddxOptions_( {
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_CheckBox>( isEnabled_, IDC_CHECK_IS_ENABLED ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_TextEdit>( topTextQuery_, IDC_EDIT_TOP_TEXT ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_TextEdit>( middleTextQuery_, IDC_EDIT_MIDDLE_TEXT ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_TextEdit>( bottomTextQuery_, IDC_EDIT_BOTTOM_TEXT ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_ComboBox>( artworkDisplayPolicy_, IDC_COMBO_ARTWORK_POLICY ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_RadioRange>( largeImageSettings_, std::initializer_list<int>{ IDC_RADIO_IMG_LIGHT, IDC_RADIO_IMG_DARK, IDC_RADIO_IMG_DISABLED } ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_RadioRange>( smallImageSettings_, std::initializer_list<int>{ IDC_RADIO_PLAYBACK_IMG_LIGHT, IDC_RADIO_PLAYBACK_IMG_DARK, IDC_RADIO_PLAYBACK_IMG_DISABLED } ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_CheckBox>( disableWhenPaused_, IDC_CHECK_DISABLE_WHEN_PAUSED ),
          qwr::ui::CreateUiDdxOption<qwr::ui::UiDdx_CheckBox>( swapSmallImages_, IDC_CHECK_SWAP_STATUS ),
      } )
{
}

PreferenceTabMain::~PreferenceTabMain()
{
    for ( auto& ddxOpt: ddxOptions_ )
    {
        ddxOpt->Option().Revert();
    }
}

HWND PreferenceTabMain::CreateTab( HWND hParent )
{
    return Create( hParent );
}

CDialogImplBase& PreferenceTabMain::Dialog()
{
    return *this;
}

const wchar_t* PreferenceTabMain::Name() const
{
    return L"Main";
}

void PreferenceTabMain::OnUiChangeRequest( int nID, bool enable )
{
}

t_uint32 PreferenceTabMain::GetState()
{
    const bool hasChanged =
        ddxOptions_.cend() != std::find_if( ddxOptions_.cbegin(), ddxOptions_.cend(), []( const auto& ddxOpt ) {
            return ddxOpt->Option().HasChanged();
        } );

    return ( preferences_state::resettable | preferences_state::dark_mode_supported | ( hasChanged ? preferences_state::changed : 0 ) );
}

void PreferenceTabMain::Apply()
{
    for ( auto& ddxOpt: ddxOptions_ )
    {
        ddxOpt->Option().Apply();
    }
}

void PreferenceTabMain::Reset()
{
    for ( auto& ddxOpt: ddxOptions_ )
    {
        ddxOpt->Option().ResetToDefault();
    }

    DoFullDdxToUi();
}

BOOL PreferenceTabMain::OnInitDialog( HWND hwndFocus, LPARAM lParam )
{
    darkModeHooks_.AddDialogWithControls( m_hWnd );

    CComboBox artworkPolicy{ GetDlgItem( IDC_COMBO_ARTWORK_POLICY ) };
    artworkPolicy.AddString( L"Prefer artwork; use large-image fallback" );
    artworkPolicy.AddString( L"Use configured large image only" );
    artworkPolicy.AddString( L"Album artwork only; no fallback image" );

    for ( auto& ddxOpt: ddxOptions_ )
    {
        ddxOpt->Ddx().SetHwnd( m_hWnd );
    }
    DoFullDdxToUi();

    UpdateArtworkStatus();
    SetTimer( kArtworkStatusTimerId, 1000 );

    helpUrl_.SetHyperLinkExtendedStyle( HLINK_UNDERLINED | HLINK_COMMANDBUTTON );
    helpUrl_.SetToolTipText( L"Title formatting help" );
    helpUrl_.SubclassWindow( GetDlgItem( IDC_LINK_FORMAT_HELP ) );

    return TRUE; // set focus to default control
}

void PreferenceTabMain::OnDdxUiChange( UINT uNotifyCode, int nID, CWindow wndCtl )
{
    auto it = std::find_if( ddxOptions_.begin(), ddxOptions_.end(), [nID]( auto& val ) {
        return val->Ddx().IsMatchingId( nID );
    } );

    if ( ddxOptions_.end() != it )
    {
        ( *it )->Ddx().ReadFromUi();
    }

    OnChanged();
    UpdateArtworkStatus();
}

void PreferenceTabMain::OnHelpUrlClick( UINT uNotifyCode, int nID, CWindow wndCtl )
{
    standard_commands::main_titleformat_help();
}

void PreferenceTabMain::OnPreviewPresenceClick( UINT uNotifyCode, int nID, CWindow wndCtl )
{
    metadb_handle_ptr handle;
    if ( !playback_control::get()->get_now_playing( handle ) )
    {
        popup_message::g_show( "Start playing a track before previewing Rich Presence.", "Rich Presence preview" );
        return;
    }

    const auto preview = fmt::format(
        "Top: {}\nMiddle: {}\nBottom: {}",
        EvaluatePreviewLine( topTextQuery_.GetCurrentValue() ),
        EvaluatePreviewLine( middleTextQuery_.GetCurrentValue() ),
        EvaluatePreviewLine( bottomTextQuery_.GetCurrentValue() ) );
    popup_message::g_show( preview.c_str(), "Rich Presence preview" );
}

void PreferenceTabMain::OnTimer( UINT_PTR timerId )
{
    if ( timerId == kArtworkStatusTimerId )
    {
        UpdateArtworkStatus();
    }
}

void PreferenceTabMain::OnDestroy()
{
    KillTimer( kArtworkStatusTimerId );
}

void PreferenceTabMain::UpdateArtworkStatus()
{
    qwr::u8string message;
    const bool hasPendingArtworkSettings = artworkDisplayPolicy_.HasChanged()
                                           || pParent_->HasPendingArtworkSettings();
    const auto appliedPolicy = artwork::NormaliseDisplayPolicy( static_cast<artwork::DisplayPolicy>( config::artworkDisplayPolicy ) );
    if ( hasPendingArtworkSettings )
    {
        message = "Artwork settings have changed; apply them to update the live status.";
    }
    else if ( appliedPolicy == artwork::DisplayPolicy::ApplicationIcon )
    {
        message = "Artwork disabled by the selected behaviour.";
    }
    else if ( !static_cast<bool>( config::enableCatboxUpload )
              && !static_cast<bool>( config::enableImgurUpload )
              && !static_cast<bool>( config::enableAlbumArtFetch )
              && !static_cast<bool>( config::enableTheAudioDbFetch ) )
    {
        message = "No artwork source is enabled.";
    }
    else
    {
        message = ArtworkFetcher::Get().GetStatus().message;
    }

    SetDlgItemText( IDC_STATIC_ARTWORK_STATUS, qwr::unicode::ToWide( fmt::format( "Status: {}", message ) ).c_str() );
}

void PreferenceTabMain::OnChanged()
{
    pParent_->OnDataChanged();
}

void PreferenceTabMain::DoFullDdxToUi()
{
    if ( !this->m_hWnd )
    {
        return;
    }

    for ( auto& ddxOpt: ddxOptions_ )
    {
        ddxOpt->Ddx().WriteToUi();
    }
}

} // namespace drp::ui
