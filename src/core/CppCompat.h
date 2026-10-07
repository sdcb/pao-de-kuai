#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * This is plan.md 附录 B 路线 B: while `src/` is being converted, the modules
 * that are already pure C expose a plain C API, but the C++ translation units
 * that have not been converted yet keep calling the old `std::string`-shaped
 * API.  Rather than rewriting those call sites twice (once now, once when the
 * file itself becomes C), they include this header, which is a thin adapter:
 *
 *     std::string  <->  Str / StrList
 *     core::JoinPath(...)  ->  WinFile_JoinPath(...)
 *
 * Rules for this file:
 *   1. It is C++ only and must never be included from a .c file.
 *   2. It must stay header-only: no .cpp counterpart, nothing to link.
 *   3. It must NOT grow new features.  Every function here has a C twin, and it
 *      disappears together with the last C++ file under src/.
 *
 * Files still depending on it (shrink this list as they are converted):
 *   src/game/AiStrategy.cpp           src/overlays/SettingsOverlay.cpp
 *   src/game/GameState.cpp            src/overlays/RoundResultOverlay.cpp
 *   src/game/RoundTraceRecorder.cpp   src/scenes/GameScene.cpp
 *   src/scenes/StartScene.cpp         src/scenes/StatsScene.cpp
 *   src/stats/AppSettings.cpp         src/stats/StatStore.cpp
 */

#include <string>
#include <string_view>
#include <vector>

#include "graphics/CppCompat.h"
#include "core/Overlay.h"
#include "core/Scene.h"
#include "core/SceneManager.h"
#include "core/Str.h"
#include "core/WinFile.h"

namespace pdk::core {

/* ---- Str <-> std::string --------------------------------------------- */

inline std::string FromStr(const Str& value)
{
    return std::string(Str_CStr(&value), static_cast<std::size_t>(value.len));
}

inline Str ToStr(std::string_view value)
{
    Str out;
    Str_Init(&out);
    Str_AppendN(&out, value.data(), static_cast<int>(value.size()));
    return out;
}

/* ---- number appending (was core/StringUtil.*) ------------------------ */

inline void AppendNumber(std::string& text, long long value)
{
    Str temp = ToStr(text);
    Str_AppendNumber(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendNumber(std::string& text, int value)
{
    AppendNumber(text, static_cast<long long>(value));
}

inline void AppendNumber(std::string& text, unsigned int value)
{
    Str temp = ToStr(text);
    Str_AppendUnsigned(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendNumber(std::string& text, unsigned long long value)
{
    Str temp = ToStr(text);
    Str_AppendUnsigned(&temp, value);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

inline void AppendPaddedNumber(std::string& text, int value, int width)
{
    Str temp = ToStr(text);
    Str_AppendPaddedNumber(&temp, value, width);
    text.assign(Str_CStr(&temp), static_cast<std::size_t>(temp.len));
    Str_Free(&temp);
}

/* ---- path and file helpers (was core/WinFile.*) ---------------------- */

inline std::string CurrentDirectory()
{
    Str out;
    Str_Init(&out);
    WinFile_CurrentDirectory(&out);
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string JoinPath(std::string_view lhs, std::string_view rhs)
{
    Str out;
    Str_Init(&out);
    const std::string left(lhs);
    const std::string right(rhs);
    WinFile_JoinPath(&out, left.c_str(), right.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string ParentPath(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_ParentPath(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string FileStem(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_FileStem(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline std::string FileExtension(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_FileExtension(&out, value.c_str());
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline bool DirectoryExists(std::string_view path)
{
    const std::string value(path);
    return WinFile_DirectoryExists(value.c_str());
}

inline bool CreateDirectories(std::string_view path)
{
    const std::string value(path);
    return WinFile_CreateDirectories(value.c_str());
}

inline std::string ReadTextFile(std::string_view path)
{
    Str out;
    Str_Init(&out);
    const std::string value(path);
    WinFile_ReadTextFile(value.c_str(), &out);
    std::string result = FromStr(out);
    Str_Free(&out);
    return result;
}

inline bool WriteTextFile(std::string_view path, std::string_view text)
{
    const std::string value(path);
    const std::string body(text);
    return WinFile_WriteTextFile(value.c_str(), body.c_str());
}

inline std::vector<std::string> ListRegularFileNames(std::string_view directory)
{
    StrList list;
    StrList_Init(&list);
    const std::string value(directory);
    WinFile_ListRegularFileNames(value.c_str(), &list);
    std::vector<std::string> result;
    result.reserve(static_cast<std::size_t>(list.count));
    for (int i = 0; i < list.count; ++i) {
        result.emplace_back(StrList_At(&list, i));
    }
    StrList_Free(&list);
    return result;
}

/* ---- scenes and overlays (plan.md S7) -------------------------------- */

using ::KeyEvent;
using ::Overlay;
using ::OverlayVtbl;
using ::Scene;
using ::SceneManager;
using ::SceneVtbl;

/*
 * The twelve scenes and overlays that are not converted yet are still C++ classes deriving from
 * `core::Scene` / `core::Overlay`, so those two names are reconstructed as abstract classes that
 * bridge onto the C vtables -- the same pattern AiStrategyClass and ExternalAiControllerClass use.
 * A converted file stops deriving from these and implements the C table directly.
 *
 * Two virtuals exist here that the old classes did not have, because the C side needs them and a
 * C vtable has no RTTI:
 *   - `OverlayClass::Expired` (default false) replaces App's dynamic_cast on InvalidMoveToast /
 *     TalkBubbleOverlay.  Both already declare a non-virtual `Expired() const`, so they become
 *     overrides without touching their declarations.
 *   - `SceneClass::RestartRound` (default false) replaces App's dynamic_cast on GameScene.
 */
class SceneClass {
public:
    virtual ~SceneClass() = default;

    virtual void OnEnter() {}
    virtual void OnExit() {}

    virtual void Update(float dt) = 0;
    virtual void Render(graphics::RenderContext& context) = 0;

    virtual bool OnMouseMove(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseDown(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseUp(float x, float y) { (void)x; (void)y; return false; }

    virtual void OnD2DResourcesLost() {}
    virtual void OnD2DResourcesRecreated() {}

    /* True when this scene handled a "restart the current round" request. */
    virtual bool RestartRound() { return false; }
};

class OverlayClass {
public:
    virtual ~OverlayClass() = default;

    virtual void Update(float dt) = 0;
    virtual void Render(graphics::RenderContext& context) = 0;

    virtual bool BlocksInputBelow() const = 0;
    virtual bool OnMouseMove(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseDown(float x, float y) { (void)x; (void)y; return false; }
    virtual bool OnMouseUp(float x, float y) { (void)x; (void)y; return false; }

    virtual bool OnKeyDown(const KeyEvent& key) { (void)key; return false; }
    /* Committed text: typed characters and IME results. */
    virtual bool OnText(const std::wstring& text) { (void)text; return false; }

    /* True while a text field has focus; the app only enables the IME then. */
    virtual bool WantsTextInput() const { return false; }
    /* In-progress IME composition; empty text ends it. */
    virtual void OnImeComposition(const std::wstring& text, int cursor) { (void)text; (void)cursor; }
    /* Caret rectangle in logical coordinates, used to place the IME candidate window. */
    virtual bool TextCaretRect(Rect& caret) const { (void)caret; return false; }

    /* True once this overlay should be dropped by the app's per-frame sweep. */
    virtual bool Expired() const { return false; }
};

namespace detail {

inline void Scene_OnEnterFn(void* user) { static_cast<SceneClass*>(user)->OnEnter(); }
inline void Scene_OnExitFn(void* user) { static_cast<SceneClass*>(user)->OnExit(); }
inline void Scene_UpdateFn(void* user, float dt) { static_cast<SceneClass*>(user)->Update(dt); }
inline void Scene_RenderFn(void* user, ::RenderContext* context)
{
    // View, not owner: the app owns the context and the C++ scene renders through the facade.
    graphics::RenderContext view(context);

    static_cast<SceneClass*>(user)->Render(view);
}
inline bool Scene_OnMouseMoveFn(void* user, float x, float y)
{
    return static_cast<SceneClass*>(user)->OnMouseMove(x, y);
}
inline bool Scene_OnMouseDownFn(void* user, float x, float y)
{
    return static_cast<SceneClass*>(user)->OnMouseDown(x, y);
}
inline bool Scene_OnMouseUpFn(void* user, float x, float y)
{
    return static_cast<SceneClass*>(user)->OnMouseUp(x, y);
}
inline void Scene_LostFn(void* user) { static_cast<SceneClass*>(user)->OnD2DResourcesLost(); }
inline void Scene_RecreatedFn(void* user)
{
    static_cast<SceneClass*>(user)->OnD2DResourcesRecreated();
}
inline bool Scene_RestartFn(void* user) { return static_cast<SceneClass*>(user)->RestartRound(); }
inline void Scene_DestroyFn(void* user) { delete static_cast<SceneClass*>(user); }

inline void Overlay_UpdateFn(void* user, float dt) { static_cast<OverlayClass*>(user)->Update(dt); }
inline void Overlay_RenderFn(void* user, ::RenderContext* context)
{
    graphics::RenderContext view(context);

    static_cast<OverlayClass*>(user)->Render(view);
}
inline bool Overlay_BlocksFn(void* user)
{
    return static_cast<OverlayClass*>(user)->BlocksInputBelow();
}
inline bool Overlay_MouseMoveFn(void* user, float x, float y)
{
    return static_cast<OverlayClass*>(user)->OnMouseMove(x, y);
}
inline bool Overlay_MouseDownFn(void* user, float x, float y)
{
    return static_cast<OverlayClass*>(user)->OnMouseDown(x, y);
}
inline bool Overlay_MouseUpFn(void* user, float x, float y)
{
    return static_cast<OverlayClass*>(user)->OnMouseUp(x, y);
}
inline bool Overlay_KeyDownFn(void* user, const KeyEvent* key)
{
    return static_cast<OverlayClass*>(user)->OnKeyDown(*key);
}
inline bool Overlay_TextFn(void* user, const wchar_t* text)
{
    return static_cast<OverlayClass*>(user)->OnText(std::wstring(text != nullptr ? text : L""));
}
inline bool Overlay_WantsTextFn(void* user)
{
    return static_cast<OverlayClass*>(user)->WantsTextInput();
}
inline void Overlay_ImeFn(void* user, const wchar_t* text, int cursor)
{
    static_cast<OverlayClass*>(user)->OnImeComposition(
        std::wstring(text != nullptr ? text : L""), cursor);
}
inline bool Overlay_CaretFn(void* user, Rect* caret)
{
    return static_cast<OverlayClass*>(user)->TextCaretRect(*caret);
}
inline bool Overlay_ExpiredFn(void* user) { return static_cast<OverlayClass*>(user)->Expired(); }
inline void Overlay_DestroyFn(void* user) { delete static_cast<OverlayClass*>(user); }

} // namespace detail

/* Takes ownership of the scene; the C side destroys it. */
inline ::Scene Transfer(SceneClass* scene)
{
    static const SceneVtbl vtbl = {
        detail::Scene_OnEnterFn,   detail::Scene_OnExitFn,      detail::Scene_UpdateFn,
        detail::Scene_RenderFn,    detail::Scene_OnMouseMoveFn, detail::Scene_OnMouseDownFn,
        detail::Scene_OnMouseUpFn, detail::Scene_LostFn,        detail::Scene_RecreatedFn,
        detail::Scene_RestartFn,   detail::Scene_DestroyFn
    };
    ::Scene handle;

    handle.vtbl = scene != nullptr ? &vtbl : NULL;
    handle.user = scene;
    return handle;
}

/* Takes ownership of the overlay; the C side destroys it. */
inline ::Overlay Transfer(OverlayClass* overlay)
{
    static const OverlayVtbl vtbl = {
        detail::Overlay_UpdateFn,  detail::Overlay_RenderFn,    detail::Overlay_BlocksFn,
        detail::Overlay_MouseMoveFn, detail::Overlay_MouseDownFn, detail::Overlay_MouseUpFn,
        detail::Overlay_KeyDownFn, detail::Overlay_TextFn,      detail::Overlay_WantsTextFn,
        detail::Overlay_ImeFn,     detail::Overlay_CaretFn,     detail::Overlay_ExpiredFn,
        detail::Overlay_DestroyFn
    };
    ::Overlay handle;

    handle.vtbl = overlay != nullptr ? &vtbl : NULL;
    handle.user = overlay;
    return handle;
}

} // namespace pdk::core
