#pragma once

/*
 * TEMPORARY TRANSITION HEADER -- DELETE WHEN THE PORT IS DONE.
 *
 * `src/app/ImeInput.*` is pure C now (plan.md S7): a value type with an Init, and WStr
 * instead of std::wstring for the composition text.
 *
 * `App` is still C++ and still writes `ime_.ReadComposition(text, cursor)` against a
 * std::wstring, so the facade reproduces exactly that shape.  Each reader converts the WStr
 * back into the caller's std::wstring, which is the only copying involved; it goes away with
 * the last C++ file under src/.
 *
 * This header grows as the rest of src/app is converted.
 */

#include "app/ImeInput.h"

#include <string>
#include <utility>

namespace pdk::app {

class ImeInput {
public:
    ImeInput() { ::ImeInput_Init(&data_); }

    void Attach(HWND hwnd) { ::ImeInput_Attach(&data_, hwnd); }
    void SetEnabled(bool enabled) { ::ImeInput_SetEnabled(&data_, enabled); }
    bool Enabled() const { return ::ImeInput_Enabled(&data_); }

    void SetCaret(const Rect& caret, const ViewTransform& view)
    {
        ::ImeInput_SetCaret(&data_, &caret, &view);
    }

    bool ReadComposition(std::wstring& text, int& cursor) const
    {
        WStr out;
        bool ok;

        WStr_Init(&out);
        ok = ::ImeInput_ReadComposition(&data_, &out, &cursor);
        text.assign(WStr_CStr(&out));
        WStr_Free(&out);
        return ok;
    }

    bool ReadResult(std::wstring& text) const
    {
        WStr out;
        bool ok;

        WStr_Init(&out);
        ok = ::ImeInput_ReadResult(&data_, &out);
        text.assign(WStr_CStr(&out));
        WStr_Free(&out);
        return ok;
    }

private:
    ::ImeInput data_;
};

} // namespace pdk::app
