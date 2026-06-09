#include "dialogs/SettingsDialog.h"

#include "app/App.h"
#include "audio/SoundIds.h"

#include <algorithm>
#include <cctype>
#include <commctrl.h>
#include <sstream>

namespace pdk::dialogs {
namespace {

constexpr int IdPlayerName = 1001;
constexpr int IdVolume = 1002;
constexpr int IdVolumeText = 1003;
constexpr int IdAi1 = 1004;
constexpr int IdAi2 = 1005;
constexpr int IdProviderList = 1006;
constexpr int IdProviderName = 1007;
constexpr int IdProviderType = 1008;
constexpr int IdProviderEndpoint = 1009;
constexpr int IdProviderApiKey = 1010;
constexpr int IdProviderModel = 1011;
constexpr int IdAddProvider = 1012;
constexpr int IdRemoveProvider = 1013;
constexpr int IdSave = 1014;
constexpr int IdCancel = 1015;

std::wstring Utf8ToWide(const std::string& text) {
    if (text.empty()) {
        return {};
    }
    const int size = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring wide(static_cast<std::size_t>(size), L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), wide.data(), size);
    return wide;
}

std::string WideToUtf8(const std::wstring& text) {
    if (text.empty()) {
        return {};
    }
    const int size = WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr);
    std::string utf8(static_cast<std::size_t>(size), '\0');
    WideCharToMultiByte(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), utf8.data(), size, nullptr, nullptr);
    return utf8;
}

std::string TrimAscii(std::string text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
        text.erase(text.begin());
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
        text.pop_back();
    }
    return text;
}

std::string UniqueProviderName(const std::map<std::string, stats::AiProviderSettings>& providers, const std::string& desired, const std::string& current = {}) {
    std::string base = TrimAscii(desired);
    if (base.empty() || base == "local") {
        base = "provider";
    }
    if ((base == current) || !providers.contains(base)) {
        return base;
    }
    for (int i = 2; i < 1000; ++i) {
        const std::string candidate = base + std::to_string(i);
        if (candidate == current || !providers.contains(candidate)) {
            return candidate;
        }
    }
    return base;
}

} // namespace

SettingsDialog::SettingsDialog(app::App& app) : app_(app) {}

SettingsDialog::~SettingsDialog() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
    if (font_) {
        DeleteObject(font_);
    }
}

bool SettingsDialog::Show(HWND owner) {
    if (hwnd_) {
        ShowWindow(hwnd_, SW_SHOWNORMAL);
        SetForegroundWindow(hwnd_);
        return true;
    }

    owner_ = owner;
    draft_ = app_.Settings();
    providerNames_.clear();
    for (const auto& [name, _] : draft_.aiProviders) {
        providerNames_.push_back(name);
    }

    INITCOMMONCONTROLSEX controls{};
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_BAR_CLASSES | ICC_STANDARD_CLASSES;
    InitCommonControlsEx(&controls);

    WNDCLASSEXW wc{};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = &SettingsDialog::StaticWndProc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.hCursor = LoadCursorW(nullptr, IDC_ARROW);
    wc.hbrBackground = reinterpret_cast<HBRUSH>(COLOR_WINDOW + 1);
    wc.lpszClassName = L"PdkSettingsDialog";
    RegisterClassExW(&wc);

    HDC dpiDc = owner ? GetDC(owner) : GetDC(nullptr);
    dpi_ = dpiDc ? static_cast<UINT>(GetDeviceCaps(dpiDc, LOGPIXELSX)) : 96;
    if (dpiDc) {
        ReleaseDC(owner, dpiDc);
    }
    font_ = CreateFontW(-MulDiv(9, static_cast<int>(dpi_), 72), 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");

    const int width = Scale(760);
    const int height = Scale(560);
    RECT ownerRect{};
    if (owner) {
        GetWindowRect(owner, &ownerRect);
    } else {
        ownerRect = {100, 100, 1380, 820};
    }
    const int x = ownerRect.left + ((ownerRect.right - ownerRect.left) - width) / 2;
    const int y = ownerRect.top + ((ownerRect.bottom - ownerRect.top) - height) / 2;

    hwnd_ = CreateWindowExW(
        WS_EX_DLGMODALFRAME | WS_EX_WINDOWEDGE,
        wc.lpszClassName,
        L"设置",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU,
        x,
        y,
        width,
        height,
        owner,
        nullptr,
        wc.hInstance,
        this);

    if (!hwnd_) {
        return false;
    }
    CreateControls();
    LoadSettingsToControls();
    ShowWindow(hwnd_, SW_SHOW);
    UpdateWindow(hwnd_);
    return true;
}

bool SettingsDialog::ProcessMessage(MSG* msg) const {
    return hwnd_ && IsDialogMessageW(hwnd_, msg) != FALSE;
}

LRESULT CALLBACK SettingsDialog::StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam) {
    SettingsDialog* dialog = nullptr;
    if (message == WM_NCCREATE) {
        auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
        dialog = static_cast<SettingsDialog*>(create->lpCreateParams);
        SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(dialog));
        dialog->hwnd_ = hwnd;
    } else {
        dialog = reinterpret_cast<SettingsDialog*>(GetWindowLongPtrW(hwnd, GWLP_USERDATA));
    }
    return dialog ? dialog->WndProc(message, wParam, lParam) : DefWindowProcW(hwnd, message, wParam, lParam);
}

LRESULT SettingsDialog::WndProc(UINT message, WPARAM wParam, LPARAM lParam) {
    switch (message) {
    case WM_COMMAND: {
        const int id = LOWORD(wParam);
        const int code = HIWORD(wParam);
        if (id == IdProviderList && code == LBN_SELCHANGE) {
            CommitSelectedProvider();
            const int selected = static_cast<int>(SendDlgItemMessageW(hwnd_, IdProviderList, LB_GETCURSEL, 0, 0));
            RefreshProviderList(selected == LB_ERR ? -1 : selected);
            RefreshAiCombos();
            return 0;
        }
        if (id == IdAddProvider && code == BN_CLICKED) {
            AddProvider();
            return 0;
        }
        if (id == IdRemoveProvider && code == BN_CLICKED) {
            RemoveProvider();
            return 0;
        }
        if (id == IdSave && code == BN_CLICKED) {
            SaveAndClose();
            return 0;
        }
        if (id == IdCancel && code == BN_CLICKED) {
            Close();
            return 0;
        }
        break;
    }
    case WM_HSCROLL:
        if (reinterpret_cast<HWND>(lParam) == GetDlgItem(hwnd_, IdVolume)) {
            const int volume = static_cast<int>(SendDlgItemMessageW(hwnd_, IdVolume, TBM_GETPOS, 0, 0));
            SetText(IdVolumeText, std::to_wstring(volume) + L"%");
            draft_.masterVolume = std::clamp(volume / 100.0f, 0.0f, 1.0f);
            app_.Audio().SetMasterVolume(draft_.masterVolume);
            return 0;
        }
        break;
    case WM_CLOSE:
        Close();
        return 0;
    case WM_NCDESTROY:
        hwnd_ = nullptr;
        return 0;
    default:
        break;
    }
    return DefWindowProcW(hwnd_, message, wParam, lParam);
}

int SettingsDialog::Scale(int value) const {
    return MulDiv(value, static_cast<int>(dpi_), 96);
}

HWND SettingsDialog::AddControl(const wchar_t* className, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height) {
    HWND control = CreateWindowExW(
        WS_EX_CONTROLPARENT,
        className,
        text,
        WS_CHILD | WS_VISIBLE | style,
        Scale(x),
        Scale(y),
        Scale(width),
        Scale(height),
        hwnd_,
        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
        GetModuleHandleW(nullptr),
        nullptr);
    if (control && font_) {
        SendMessageW(control, WM_SETFONT, reinterpret_cast<WPARAM>(font_), TRUE);
    }
    return control;
}

void SettingsDialog::CreateControls() {
    AddControl(L"STATIC", L"玩家名", 0, -1, 24, 24, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IdPlayerName, 108, 22, 230, 26);

    AddControl(L"STATIC", L"音量", 0, -1, 380, 24, 48, 24);
    AddControl(TRACKBAR_CLASSW, L"", WS_TABSTOP | TBS_AUTOTICKS, IdVolume, 430, 20, 220, 32);
    SendDlgItemMessageW(hwnd_, IdVolume, TBM_SETRANGE, TRUE, MAKELPARAM(0, 100));
    AddControl(L"STATIC", L"80%", 0, IdVolumeText, 660, 24, 48, 24);

    AddControl(L"STATIC", L"AI1", 0, -1, 24, 70, 60, 24);
    AddControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IdAi1, 108, 66, 230, 220);
    AddControl(L"STATIC", L"AI2", 0, -1, 380, 70, 60, 24);
    AddControl(L"COMBOBOX", L"", WS_TABSTOP | CBS_DROPDOWNLIST | WS_VSCROLL, IdAi2, 430, 66, 230, 220);

    AddControl(L"STATIC", L"AI Providers", 0, -1, 24, 118, 180, 24);
    AddControl(L"LISTBOX", L"", WS_TABSTOP | WS_BORDER | LBS_NOTIFY | WS_VSCROLL, IdProviderList, 24, 146, 185, 280);
    AddControl(L"BUTTON", L"新增", WS_TABSTOP, IdAddProvider, 24, 438, 86, 30);
    AddControl(L"BUTTON", L"删除", WS_TABSTOP, IdRemoveProvider, 123, 438, 86, 30);

    AddControl(L"STATIC", L"名称", 0, -1, 235, 146, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IdProviderName, 320, 143, 390, 26);
    AddControl(L"STATIC", L"type", 0, -1, 235, 188, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IdProviderType, 320, 185, 390, 26);
    AddControl(L"STATIC", L"endpoint", 0, -1, 235, 230, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IdProviderEndpoint, 320, 227, 390, 26);
    AddControl(L"STATIC", L"apiKey", 0, -1, 235, 272, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL | ES_PASSWORD, IdProviderApiKey, 320, 269, 390, 26);
    AddControl(L"STATIC", L"model", 0, -1, 235, 314, 80, 24);
    AddControl(L"EDIT", L"", WS_TABSTOP | WS_BORDER | ES_AUTOHSCROLL, IdProviderModel, 320, 311, 390, 26);

    AddControl(L"STATIC", L"apiKey 保存到 appsettings.json 时会使用 Windows DPAPI 加密；明文旧配置仍可读取。", 0, -1, 235, 356, 480, 44);

    AddControl(L"BUTTON", L"保存", WS_TABSTOP | BS_DEFPUSHBUTTON, IdSave, 520, 474, 90, 34);
    AddControl(L"BUTTON", L"取消", WS_TABSTOP, IdCancel, 620, 474, 90, 34);
}

void SettingsDialog::LoadSettingsToControls() {
    SetText(IdPlayerName, Utf8ToWide(draft_.playerName));
    const int volume = std::clamp(static_cast<int>(draft_.masterVolume * 100.0f + 0.5f), 0, 100);
    SendDlgItemMessageW(hwnd_, IdVolume, TBM_SETPOS, TRUE, volume);
    SetText(IdVolumeText, std::to_wstring(volume) + L"%");
    RefreshProviderList(providerNames_.empty() ? -1 : 0);
    RefreshAiCombos();
}

void SettingsDialog::RefreshProviderList(int selectIndex) {
    HWND list = GetDlgItem(hwnd_, IdProviderList);
    SendMessageW(list, LB_RESETCONTENT, 0, 0);
    providerNames_.clear();
    for (const auto& [name, _] : draft_.aiProviders) {
        providerNames_.push_back(name);
        SendMessageW(list, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(name).c_str()));
    }
    if (!providerNames_.empty()) {
        selectedProvider_ = std::clamp(selectIndex < 0 ? selectedProvider_ : selectIndex, 0, static_cast<int>(providerNames_.size()) - 1);
        SendMessageW(list, LB_SETCURSEL, selectedProvider_, 0);
    } else {
        selectedProvider_ = -1;
    }
    LoadSelectedProvider();
}

void SettingsDialog::RefreshAiCombos() {
    auto refresh = [&](int id, const std::string& selected) {
        HWND combo = GetDlgItem(hwnd_, id);
        SendMessageW(combo, CB_RESETCONTENT, 0, 0);
        SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"local"));
        for (const std::string& name : providerNames_) {
            SendMessageW(combo, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(Utf8ToWide(name).c_str()));
        }
        SetComboSelectionByText(id, Utf8ToWide(selected.empty() ? "local" : selected));
        if (ComboSelection(id) == CB_ERR) {
            SetComboSelectionByText(id, L"local");
        }
    };
    refresh(IdAi1, draft_.ai1);
    refresh(IdAi2, draft_.ai2);
}

void SettingsDialog::LoadSelectedProvider() {
    const bool hasSelection = selectedProvider_ >= 0 && selectedProvider_ < static_cast<int>(providerNames_.size());
    const std::string name = hasSelection ? providerNames_[static_cast<std::size_t>(selectedProvider_)] : "";
    const auto it = hasSelection ? draft_.aiProviders.find(name) : draft_.aiProviders.end();
    SetText(IdProviderName, hasSelection ? Utf8ToWide(name) : L"");
    SetText(IdProviderType, it != draft_.aiProviders.end() ? Utf8ToWide(it->second.type) : L"");
    SetText(IdProviderEndpoint, it != draft_.aiProviders.end() ? Utf8ToWide(it->second.endpoint) : L"");
    SetText(IdProviderApiKey, it != draft_.aiProviders.end() ? Utf8ToWide(it->second.apiKey) : L"");
    SetText(IdProviderModel, it != draft_.aiProviders.end() ? Utf8ToWide(it->second.model) : L"");
}

void SettingsDialog::CommitSelectedProvider() {
    if (selectedProvider_ < 0 || selectedProvider_ >= static_cast<int>(providerNames_.size())) {
        return;
    }

    const std::string oldName = providerNames_[static_cast<std::size_t>(selectedProvider_)];
    const std::string newName = UniqueProviderName(draft_.aiProviders, WideToUtf8(Text(IdProviderName)), oldName);
    stats::AiProviderSettings provider;
    provider.type = WideToUtf8(Text(IdProviderType));
    provider.endpoint = WideToUtf8(Text(IdProviderEndpoint));
    provider.apiKey = WideToUtf8(Text(IdProviderApiKey));
    provider.model = WideToUtf8(Text(IdProviderModel));

    draft_.aiProviders.erase(oldName);
    draft_.aiProviders[newName] = std::move(provider);
    if (draft_.ai1 == oldName) {
        draft_.ai1 = newName;
    }
    if (draft_.ai2 == oldName) {
        draft_.ai2 = newName;
    }
}

void SettingsDialog::AddProvider() {
    CommitSelectedProvider();
    const std::string name = UniqueProviderName(draft_.aiProviders, "provider");
    draft_.aiProviders[name] = stats::AiProviderSettings{"openai", {}, {}, {}};
    RefreshProviderList();
    const auto it = std::find(providerNames_.begin(), providerNames_.end(), name);
    if (it != providerNames_.end()) {
        selectedProvider_ = static_cast<int>(std::distance(providerNames_.begin(), it));
        SendDlgItemMessageW(hwnd_, IdProviderList, LB_SETCURSEL, selectedProvider_, 0);
        LoadSelectedProvider();
    }
    RefreshAiCombos();
    SetFocus(GetDlgItem(hwnd_, IdProviderName));
}

void SettingsDialog::RemoveProvider() {
    if (selectedProvider_ < 0 || selectedProvider_ >= static_cast<int>(providerNames_.size())) {
        return;
    }
    const std::string name = providerNames_[static_cast<std::size_t>(selectedProvider_)];
    draft_.aiProviders.erase(name);
    if (draft_.ai1 == name) {
        draft_.ai1 = "local";
    }
    if (draft_.ai2 == name) {
        draft_.ai2 = "local";
    }
    RefreshProviderList(std::min(selectedProvider_, static_cast<int>(draft_.aiProviders.size()) - 1));
    RefreshAiCombos();
}

void SettingsDialog::SaveAndClose() {
    draft_.playerName = WideToUtf8(Text(IdPlayerName));
    draft_.masterVolume = std::clamp(static_cast<int>(SendDlgItemMessageW(hwnd_, IdVolume, TBM_GETPOS, 0, 0)) / 100.0f, 0.0f, 1.0f);
    draft_.ai1 = WideToUtf8(ComboText(IdAi1));
    draft_.ai2 = WideToUtf8(ComboText(IdAi2));
    if (draft_.ai1.empty()) {
        draft_.ai1 = "local";
    }
    if (draft_.ai2.empty()) {
        draft_.ai2 = "local";
    }
    CommitSelectedProvider();
    app_.Settings() = draft_;
    app_.Audio().SetMasterVolume(draft_.masterVolume);
    app_.SaveSettings();
    app_.Audio().Play(audio::SoundId::Confirm);
    Close();
}

void SettingsDialog::Close() {
    if (hwnd_) {
        DestroyWindow(hwnd_);
    }
}

void SettingsDialog::SetText(int id, const std::wstring& text) {
    SetWindowTextW(GetDlgItem(hwnd_, id), text.c_str());
}

std::wstring SettingsDialog::Text(int id) const {
    HWND control = GetDlgItem(hwnd_, id);
    const int length = GetWindowTextLengthW(control);
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    GetWindowTextW(control, text.data(), length + 1);
    text.resize(static_cast<std::size_t>(length));
    return text;
}

int SettingsDialog::ComboSelection(int id) const {
    return static_cast<int>(SendDlgItemMessageW(hwnd_, id, CB_GETCURSEL, 0, 0));
}

std::wstring SettingsDialog::ComboText(int id) const {
    HWND combo = GetDlgItem(hwnd_, id);
    const int selected = ComboSelection(id);
    if (selected == CB_ERR) {
        return {};
    }
    const int length = static_cast<int>(SendMessageW(combo, CB_GETLBTEXTLEN, selected, 0));
    std::wstring text(static_cast<std::size_t>(length + 1), L'\0');
    SendMessageW(combo, CB_GETLBTEXT, selected, reinterpret_cast<LPARAM>(text.data()));
    text.resize(static_cast<std::size_t>(length));
    return text;
}

void SettingsDialog::SetComboSelectionByText(int id, const std::wstring& text) {
    const int found = static_cast<int>(SendDlgItemMessageW(hwnd_, id, CB_FINDSTRINGEXACT, static_cast<WPARAM>(-1), reinterpret_cast<LPARAM>(text.c_str())));
    SendDlgItemMessageW(hwnd_, id, CB_SETCURSEL, found == CB_ERR ? 0 : found, 0);
}

} // namespace pdk::dialogs
