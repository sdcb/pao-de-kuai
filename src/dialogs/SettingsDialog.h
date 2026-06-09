#pragma once

#include "stats/AppSettings.h"

#include <string>
#include <vector>

#include <windows.h>

namespace pdk::app {
class App;
}

namespace pdk::dialogs {

class SettingsDialog {
public:
    explicit SettingsDialog(app::App& app);
    ~SettingsDialog();

    bool Show(HWND owner);
    bool IsOpen() const { return hwnd_ != nullptr; }
    HWND Hwnd() const { return hwnd_; }
    bool ProcessMessage(MSG* msg) const;

private:
    static LRESULT CALLBACK StaticWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam);
    LRESULT WndProc(UINT message, WPARAM wParam, LPARAM lParam);

    void CreateControls();
    void LoadSettingsToControls();
    void RefreshProviderList(int selectIndex = -1);
    void RefreshAiCombos();
    void LoadSelectedProvider();
    void CommitSelectedProvider();
    void AddProvider();
    void RemoveProvider();
    void SaveAndClose();
    void Close();

    int Scale(int value) const;
    HWND AddControl(const wchar_t* className, const wchar_t* text, DWORD style, int id, int x, int y, int width, int height);
    void SetText(int id, const std::wstring& text);
    std::wstring Text(int id) const;
    int ComboSelection(int id) const;
    std::wstring ComboText(int id) const;
    void SetComboSelectionByText(int id, const std::wstring& text);

    app::App& app_;
    HWND owner_{};
    HWND hwnd_{};
    HFONT font_{};
    UINT dpi_{96};
    stats::AppSettings draft_;
    std::vector<std::string> providerNames_;
    int selectedProvider_{-1};
};

} // namespace pdk::dialogs
