/****************************************************************************
 Copyright (c) 2019-present Axmol Engine contributors (see AUTHORS.md).

 https://axmol.dev/

 Permission is hereby granted, free of charge, to any person obtaining a copy
 of this software and associated documentation files (the "Software"), to deal
 in the Software without restriction, including without limitation the rights
 to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 copies of the Software, and to permit persons to whom the Software is
 furnished to do so, subject to the following conditions:

 The above copyright notice and this permission notice shall be included in
 all copies or substantial portions of the Software.

 THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 THE SOFTWARE.
 ****************************************************************************/
#pragma once

#include "axmol/platform/PlatformConfig.h"

#include <windows.ui.xaml.media.dxinterop.h>
#include <windows.ui.xaml.controls.h>
#include <windows.ui.core.h>
#include <windows.foundation.h>
#include <wrl/event.h>
#include <wrl/implements.h>

namespace ax::winrt
{
using ICoreDispatcher    = ABI::Windows::UI::Core::ICoreDispatcher;
using IDispatchedHandler = ABI::Windows::UI::Core::IDispatchedHandler;
using IAsyncAction       = ABI::Windows::Foundation::IAsyncAction;
using ISwapChainPanel    = ABI::Windows::UI::Xaml::Controls::ISwapChainPanel;
using IDependencyObject  = ABI::Windows::UI::Xaml::IDependencyObject;
using IUIElement         = ABI::Windows::UI::Xaml::IUIElement;

// Creates a COM/WinRT callback object for the specified interface type (_Ty)
// that is implemented with Free‑Threaded Marshaler (FtmBase) support.
//
// This helper wraps Microsoft::WRL::Callback with an Implements<> type that
// includes FtmBase, making the resulting object agile across threads.
// This is especially useful when passing the handler to APIs like
// ICoreDispatcher::RunAsync, which may invoke the callback on a different thread.
//
// Template parameters:
//   _Ty  - The COM/WinRT interface type to implement (e.g. ABI::Windows::UI::Core::IDispatchedHandler)
//   _Fty - The callable type (lambda, functor, etc.) providing the implementation
//
// Parameters:
//   func - A callable object implementing the interface's Invoke method
//
// Returns:
//   A Microsoft::WRL::ComPtr-compatible callback object implementing _Ty with FTM support.
template <typename _Ty, typename _Fty>
static auto makeFtmHandler(_Fty&& func)
{
    using Impl = Microsoft::WRL::Implements<Microsoft::WRL::RuntimeClassFlags<Microsoft::WRL::ClassicCom>, _Ty,
                                            Microsoft::WRL::FtmBase>;
    return Microsoft::WRL::Callback<Impl>(std::forward<_Fty>(func));
}

template <typename _Fty>
static HRESULT runOnUIThread(const ComPtr<ICoreDispatcher>& dispatcher, _Fty&& func)
{
    using namespace ABI::Windows::UI::Core;

    boolean hasThreadAccess = FALSE;
    HRESULT hr              = dispatcher->get_HasThreadAccess(&hasThreadAccess);
    if (FAILED(hr))
        return hr;

    if (hasThreadAccess)
    {
        return func();
    }

    struct AutoHandle
    {
        explicit AutoHandle(HANDLE h) : _h(h) {}
        ~AutoHandle()
        {
            if (_h)
                ::CloseHandle(_h);
        }
        HANDLE get() const { return _h; }
        explicit operator bool() const { return _h != nullptr; }

    private:
        HANDLE _h;
    };

    AutoHandle waitEvent{::CreateEventExW(nullptr, nullptr, CREATE_EVENT_MANUAL_RESET, EVENT_ALL_ACCESS)};
    if (!waitEvent)
        return E_FAIL;

    HRESULT hr2 = E_FAIL;

    auto handler = makeFtmHandler<IDispatchedHandler>([&]() -> HRESULT {
        hr2 = func();
        ::SetEvent(waitEvent.get());
        return S_OK;
    });

    ComPtr<IAsyncAction> asyncAction;
    hr = dispatcher->RunAsync(CoreDispatcherPriority_Normal, handler.Get(), &asyncAction);
    if (FAILED(hr))
        return hr;

    auto waitResult = ::WaitForSingleObjectEx(waitEvent.get(), 10 * 1000, TRUE);
    if (waitResult != WAIT_OBJECT_0)
    {
        std::terminate();
        return E_FAIL;
    }

    return hr2;
}

}  // namespace ax::winrt
