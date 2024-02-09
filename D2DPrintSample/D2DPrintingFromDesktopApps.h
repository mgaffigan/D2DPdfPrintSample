//// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//// PARTICULAR PURPOSE.
////
//// Copyright (c) Microsoft Corporation. All rights reserved

#pragma once

// SafeRelease inline function.
template <class Interface> inline void SafeRelease(
    Interface** interfaceToRelease
)
{
    if (*interfaceToRelease != nullptr)
    {
        (*interfaceToRelease)->Release();

        (*interfaceToRelease) = nullptr;
    }
}

// Macros.
#ifndef Assert
#if defined(DEBUG) || defined(_DEBUG) || DBG
#define Assert(b) if (!(b)) { OutputDebugStringA("Assert: " #b "\n"); }
#else
#define Assert(b)
#endif // DEBUG || _DEBUG
#endif

#ifndef HINST_THISCOMPONENT
EXTERN_C IMAGE_DOS_HEADER __ImageBase;
#define HINST_THISCOMPONENT ((HINSTANCE)&__ImageBase)
#endif

// The main class for this sample application.
class DemoApp
{
public:
    DemoApp();

    ~DemoApp();

    HRESULT Initialize();

    void RunMessageLoop();

private:
    HRESULT CreateDeviceIndependentResources();

    HRESULT CreateDeviceContext();

    HRESULT CreateDeviceResources();

    void DiscardDeviceResources();

    HRESULT CreateGridPatternBrush(
        _Outptr_ ID2D1ImageBrush** imageBrush
    );

    HRESULT OnRender();

    winrt::Windows::Foundation::IAsyncAction OnPrint();

    HRESULT InitializePrintJob();

    HRESULT GetPrintTicketFromDevmode(
        _In_ PCTSTR printerName,
        _In_reads_bytes_(devModeSize) PDEVMODE devMode,
        WORD devModeSize,
        _Out_ LPSTREAM* printTicketStream
    );

    HRESULT FinalizePrintJob();

    HRESULT DrawToContext(
        _In_ ID2D1DeviceContext* d2dContext,
        UINT pageNumber, // 1-based page number
        BOOL printing
    );

    void OnChar(
        SHORT key
    );

    void ToggleMultiPageMode();

    void OnResize();

    void OnVScroll(
        WPARAM wParam,
        LPARAM lParam
    );

    void ResetScrollBar();

    void OnMouseWheel(
        WPARAM wParam,
        LPARAM lParam
    );

    D2D1_SIZE_U CalculateD2DWindowSize();

    static LRESULT CALLBACK ParentWndProc(
        HWND hWnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    static LRESULT CALLBACK ChildWndProc(
        HWND hWnd,
        UINT message,
        WPARAM wParam,
        LPARAM lParam
    );

    static HRESULT LoadBitmapFromFile(
        _In_ ID2D1DeviceContext* d2dContext,
        _In_ IWICImagingFactory* wicFactory,
        _In_ PCWSTR uri,
        UINT destinationWidth,
        UINT destinationHeight,
        _Outptr_ ID2D1Bitmap** bitmap
    );

    LRESULT OnClose();

private:
    bool m_resourcesValid;                      // Whether or not the device-dependent resources are ready to use.

    HWND m_parentHwnd;                          // The outer window containing the scroll bar and inner window.
    
    // Device-independent resources.
    ID2D1Factory1* m_d2dFactory;
    IWICImagingFactory2* m_wicFactory;

    // Device-dependent resources.
    IDXGIDevice* m_dxgiDevice;
    ID2D1Device* m_d2dDevice;

    // Printing-specific resources.
    IStream* m_jobPrintTicketStream;
    ID2D1PrintControl* m_printControl;
    IPrintDocumentPackageTarget* m_documentTarget;
    D2DPrintJobChecker* m_printJobChecker;

    // Page size (in DIPs).
    float m_pageHeight;
    float m_pageWidth;
};