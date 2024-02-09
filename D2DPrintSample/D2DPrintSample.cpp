//// THIS CODE AND INFORMATION IS PROVIDED "AS IS" WITHOUT WARRANTY OF
//// ANY KIND, EITHER EXPRESSED OR IMPLIED, INCLUDING BUT NOT LIMITED TO
//// THE IMPLIED WARRANTIES OF MERCHANTABILITY AND/OR FITNESS FOR A
//// PARTICULAR PURPOSE.
////
//// Copyright (c) Microsoft Corporation. All rights reserved

#define WINVER 0x0A00 // Windows 10
#define _WIN32_WINNT 0x0A00 // Windows 10

#include <winrt\base.h>
#include <winrt\windows.foundation.h>
#include <winrt\windows.storage.h>
#include <winrt\windows.data.pdf.h>
#pragma comment(lib, "windowsapp")

// DirectX header files.
#include <d2d1_1.h>
#include <d3d11.h>
#include <dwrite.h>
#include <wincodec.h>
#include <Windows.h>
#include <WinUser.h>

#include <xpsobjectmodel_1.h>
#include <DocumentTarget.h>

#include <commdlg.h>
#include <wchar.h>
#include <math.h>
#include <Prntvpt.h>
#include <Strsafe.h>
#include <wil\com.h>
#include <windows.data.pdf.interop.h>

#include "D2DPrintJobChecker.h"
#include "D2DPrintingFromDesktopApps.h"

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Data::Pdf;

static const FLOAT PAGE_WIDTH_IN_DIPS = 8.5f * 96.0f;     // 8.5 inches
static const FLOAT PAGE_HEIGHT_IN_DIPS = 11.0f * 96.0f;    // 11 inches
static const FLOAT PAGE_MARGIN_IN_DIPS = 96.0f;            // 1 inch
static const FLOAT FRAME_HEIGHT_IN_DIPS = 400.0f;           // 400 DIPs
static const FLOAT HOURGLASS_SIZE = 200.0f;           // 200 DIPs

// Provides the main entry point to the application.
int WINAPI WinMain(
	_In_        HINSTANCE /* hInstance */,
	_In_opt_    HINSTANCE /* hPrevInstance */,
	_In_        LPSTR /* lpCmdLine */,
	_In_        int /* nCmdShow */
)
{
	// Ignore the return value because we want to continue running even in the
	// unlikely event that HeapSetInformation fails.
	BOOL succeded = HeapSetInformation(nullptr, HeapEnableTerminationOnCorruption, nullptr, 0);
	if (!succeded)
	{
		// Enable Heap Termination upon corruption failed.
		// For this sample, we ignore when this fails and continue running.
	}

	winrt::init_apartment(winrt::apartment_type::single_threaded);

	DemoApp app;
	if (SUCCEEDED(app.Initialize()))
	{
		app.RunMessageLoop();
	}

	return 0;
}

// The main window message loop.
void DemoApp::RunMessageLoop()
{
	MSG message;

	while (GetMessage(&message, nullptr, 0, 0))
	{
		TranslateMessage(&message);
		DispatchMessage(&message);
	}
}

// Initializes members.
DemoApp::DemoApp() :
	m_resourcesValid(false),
	m_parentHwnd(nullptr),
	m_d2dFactory(nullptr),
	m_wicFactory(nullptr),
	m_d2dDevice(nullptr),
	m_jobPrintTicketStream(nullptr),
	m_printControl(nullptr),
	m_documentTarget(nullptr),
	m_printJobChecker(nullptr),
	m_pageHeight(PAGE_HEIGHT_IN_DIPS),
	m_pageWidth(PAGE_WIDTH_IN_DIPS)
{
}

// Releases resources.
DemoApp::~DemoApp()
{
	// Release device-dependent resources.
	SafeRelease(&m_dxgiDevice);
	SafeRelease(&m_d2dDevice);

	// Release printing-specific resources.
	SafeRelease(&m_jobPrintTicketStream);
	SafeRelease(&m_printControl);
	SafeRelease(&m_documentTarget);
	SafeRelease(&m_printJobChecker);

	// Release factories.
	SafeRelease(&m_d2dFactory);
	SafeRelease(&m_wicFactory);
}

// Creates the application window and initializes
// device-independent and device-dependent resources.
HRESULT DemoApp::Initialize()
{
	// Initialize device-indpendent resources, such
	// as the Direct2D factory.
	HRESULT hr = CreateDeviceIndependentResources();

	if (SUCCEEDED(hr))
	{
		// Register the parent window class.
		WNDCLASSEX wcex = { sizeof(WNDCLASSEX) };
		wcex.style = CS_HREDRAW | CS_VREDRAW;
		wcex.lpfnWndProc = DemoApp::ParentWndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = sizeof(LONG_PTR);
		wcex.hInstance = HINST_THISCOMPONENT;
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = nullptr;
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = L"DemoAppWindow";

		RegisterClassEx(&wcex);

		// Because the CreateWindow function takes its size in pixels, we
		// obtain the system DPI and use it to scale the window size.

		// Create the parent window.
		m_parentHwnd = CreateWindow(
			L"DemoAppWindow",
			L"Direct2D desktop app printing sample - Press 'p' to print, press <Tab> to toggle multi-page mode",
			WS_OVERLAPPEDWINDOW | WS_VSCROLL,
			CW_USEDEFAULT,
			CW_USEDEFAULT,
			640, 480,
			nullptr,
			nullptr,
			HINST_THISCOMPONENT,
			this
		);

		hr = m_parentHwnd ? S_OK : E_FAIL;

		if (SUCCEEDED(hr))
		{
			ShowWindow(m_parentHwnd, SW_SHOWNORMAL);
			UpdateWindow(m_parentHwnd);
		}
	}

	// Create D2D device context and device-dependent resources.
	if (SUCCEEDED(hr))
	{
		hr = CreateDeviceResources();
	}

	return hr;
}

// Calculates the size of the D2D (child) window.
D2D1_SIZE_U DemoApp::CalculateD2DWindowSize()
{
	RECT rc;
	GetClientRect(m_parentHwnd, &rc);

	D2D1_SIZE_U d2dWindowSize = { 0 };
	d2dWindowSize.width = rc.right;
	d2dWindowSize.height = rc.bottom;

	return d2dWindowSize;
}

// Creates resources which are not bound to any device.
// Their lifetimes effectively extend for the duration
// of the app.
HRESULT DemoApp::CreateDeviceIndependentResources()
{
	HRESULT hr = S_OK;

	if (SUCCEEDED(hr))
	{
		// Create a Direct2D factory.
		D2D1_FACTORY_OPTIONS options;
		ZeroMemory(&options, sizeof(D2D1_FACTORY_OPTIONS));

#if defined(_DEBUG)
		// If the project is in a debug build, enable Direct2D debugging via SDK Layers
		options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif

		hr = D2D1CreateFactory(
			D2D1_FACTORY_TYPE_MULTI_THREADED,
			options,
			&m_d2dFactory
		);
	}
	if (SUCCEEDED(hr))
	{
		// Create a WIC factory.
		hr = CoCreateInstance(
			CLSID_WICImagingFactory,
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&m_wicFactory)
		);
	}

	return hr;
}

// Create D2D context for display (Direct3D) device
HRESULT DemoApp::CreateDeviceContext()
{
	HRESULT hr = S_OK;

	// Get the size of the child window.
	D2D1_SIZE_U size = CalculateD2DWindowSize();

	// Create a D3D device and a swap chain sized to the child window.
	UINT createDeviceFlags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;

#ifdef _DEBUG
	createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

	D3D_DRIVER_TYPE driverTypes[] =
	{
		D3D_DRIVER_TYPE_HARDWARE,
		D3D_DRIVER_TYPE_WARP,
	};
	UINT countOfDriverTypes = ARRAYSIZE(driverTypes);

	ID3D11Device* d3dDevice = nullptr;
	for (UINT driverTypeIndex = 0; driverTypeIndex < countOfDriverTypes; driverTypeIndex++)
	{
		hr = D3D11CreateDevice(
			nullptr,       // use default adapter
			driverTypes[driverTypeIndex],
			nullptr,       // no external software rasterizer
			createDeviceFlags,
			nullptr,       // use default set of feature levels
			0,
			D3D11_SDK_VERSION,
			&d3dDevice,
			nullptr,       // do not care about what feature level is chosen
			nullptr        // do not retain D3D device context
		);

		if (SUCCEEDED(hr))
		{
			break;
		}
	}

	if (SUCCEEDED(hr))
	{
		// Get a DXGI device interface from the D3D device.
		hr = d3dDevice->QueryInterface(&m_dxgiDevice);
	}
	if (SUCCEEDED(hr))
	{
		// Create a D2D device from the DXGI device.
		hr = m_d2dFactory->CreateDevice(
			m_dxgiDevice,
			&m_d2dDevice
		);
	}

	SafeRelease(&d3dDevice);
	return hr;
}

// This method creates resources which are bound to a particular
// Direct3D device. It's all centralized here, in case the resources
// need to be recreated in case of Direct3D device loss (e.g. display
// change, remoting, removal of video card, etc). The resources created
// here can be used by multiple Direct2D device contexts (in this
// sample, one for display and another for print) which are created
// from the same Direct2D device.
HRESULT DemoApp::CreateDeviceResources()
{
	HRESULT hr = S_OK;

	if (!m_resourcesValid)
	{
		hr = CreateDeviceContext();
	}

	if (FAILED(hr))
	{
		DiscardDeviceResources();
	}
	else
	{
		m_resourcesValid = true;
	}

	return hr;
}

// Creates a bitmap-backed pattern brush to be used for the grid
// background in the scene.
HRESULT DemoApp::CreateGridPatternBrush(
	_Outptr_ ID2D1ImageBrush** imageBrush
)
{
	HRESULT hr = S_OK;
	return hr;
}

// Discards device-specific resources which need to be recreated
// when a Direct3D device is lost.
void DemoApp::DiscardDeviceResources()
{
	SafeRelease(&m_d2dDevice);

	m_resourcesValid = false;
}

// Draws the scene to a rendering device context or a printing device context.
// If the "printing" parameter is set, this function will add margins to
// the target and render the text across two pages. Otherwise, it fits the
// content to the target and renders the text in one block.
HRESULT DemoApp::DrawToContext(
	_In_ ID2D1DeviceContext* d2dContext,
	UINT pageNumber,
	BOOL printing
)
{
	return S_OK;
}


// Called whenever the application needs to display the client
// window. Draws a 2D scene onto the rendering device context.
// Note that this function will automatically discard device-specific
// resources if the Direct3D device disappears during function
// invocation and will recreate the resources the next time it's
// invoked.
HRESULT DemoApp::OnRender()
{
	HRESULT hr = S_OK;

	if (!m_resourcesValid)
	{
		hr = CreateDeviceResources();
	}

	return hr;
}

// Called whenever the application receives a keystroke (a
// WM_CHAR message). Starts printing or toggles multi-page
// mode, depending on the key pressed.
void DemoApp::OnChar(SHORT key)
{
	if ((key == 'p') || (key == 'P'))
	{
		OnPrint();
	}
	else if (key == '\t')
	{
		ToggleMultiPageMode();
	}
}

// Switches the application between single-page and multi-page mode.
void DemoApp::ToggleMultiPageMode()
{
}

// Called whenever the application begins a print job. Initializes
// the printing subsystem, draws the scene to a printing device
// context, and commits the job to the printing subsystem.
IAsyncAction DemoApp::OnPrint()
{
	try
	{
		if (!m_resourcesValid)
		{
			winrt::check_hresult(CreateDeviceResources());
		}

		// Initialize printing-specific resources and prepare the
		// printing subsystem for a job.
		winrt::check_hresult(InitializePrintJob());

		// Create a D2D Device Context dedicated for the print job.
		{
			wil::com_ptr<ID2D1DeviceContext> d2dContextForPrint;
			winrt::check_hresult(m_d2dDevice->CreateDeviceContext(
				D2D1_DEVICE_CONTEXT_OPTIONS_NONE,
				&d2dContextForPrint
			));


			for (INT pageIndex = 1; pageIndex <= 1; pageIndex++)
			{
				wil::com_ptr<ID2D1CommandList> commandList;
				winrt::check_hresult(d2dContextForPrint->CreateCommandList(&commandList));

				// Create, draw, and add a Direct2D Command List for each page.
				d2dContextForPrint->SetTarget(commandList.get());
				//hr = DrawToContext(d2dContextForPrint, pageIndex, TRUE);  // TRUE specifies rendering for printing

				StorageFile file = co_await StorageFile::GetFileFromPathAsync(L"C:\\dev\\gitroot\\D2DPrintSample\\D2DPrintSample\\example.pdf");
				PdfDocument pdfDoc = co_await PdfDocument::LoadFromFileAsync(file);
				IPdfPage pdfPage = pdfDoc.GetPage(0);

				wil::com_ptr<IPdfRendererNative> pPdfRendererNative;
				winrt::check_hresult(PdfCreateRenderer(m_dxgiDevice, &pPdfRendererNative));

				d2dContextForPrint->BeginDraw();

				PDF_RENDER_PARAMS renderParams = {}; // You can set additional parameters if needed.
				winrt::check_hresult(pPdfRendererNative->RenderPageToDeviceContext(winrt::get_unknown(pdfPage), d2dContextForPrint.get(), &renderParams));

				d2dContextForPrint->EndDraw();

				winrt::check_hresult(commandList->Close());

				winrt::check_hresult(m_printControl->AddPage(commandList.get(), D2D1::SizeF(m_pageWidth, m_pageHeight), nullptr));
			}

			// Release the print device context.
		}

		// Send the job to the printing subsystem and discard
		// printing-specific resources.
		winrt::check_hresult(FinalizePrintJob());
	}
	catch (winrt::hresult_error const& hr)
	{
		if (hr.code() == D2DERR_RECREATE_TARGET)
		{
			DiscardDeviceResources();
		}
		throw;
	}
}

// Brings up a Print Dialog to collect user print
// settings, then creates and initializes a print
// control object properly for a new print job.
HRESULT DemoApp::InitializePrintJob()
{
	HRESULT hr = S_OK;
	WCHAR messageBuffer[512] = { 0 };

	// Bring up Print Dialog and receive user print settings.
	PRINTDLGEX printDialogEx = { 0 };
	printDialogEx.lStructSize = sizeof(PRINTDLGEX);
	printDialogEx.Flags = PD_HIDEPRINTTOFILE | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
	printDialogEx.hwndOwner = m_parentHwnd;
	printDialogEx.nStartPage = START_PAGE_GENERAL;

	HRESULT hrPrintDlgEx = PrintDlgEx(&printDialogEx);

	if (FAILED(hrPrintDlgEx))
	{
		// Error occured.
		StringCchPrintf(
			messageBuffer,
			ARRAYSIZE(messageBuffer),
			L"Error 0x%4X occured during printer selection and/or setup.",
			hrPrintDlgEx
		);
		MessageBox(m_parentHwnd, messageBuffer, L"Message", MB_OK);
		hr = hrPrintDlgEx;
	}
	else if (printDialogEx.dwResultAction == PD_RESULT_APPLY)
	{
		// User clicks the Apply button and later clicks the Cancel button.
		// For simpicity, this sample skips print settings recording.
		hr = E_FAIL;
	}
	else if (printDialogEx.dwResultAction == PD_RESULT_CANCEL)
	{
		// User clicks the Cancel button.
		hr = E_FAIL;
	}

	// Retrieve DEVNAMES from print dialog.
	DEVNAMES* devNames = nullptr;
	if (SUCCEEDED(hr))
	{
		if (printDialogEx.hDevNames != nullptr)
		{
			devNames = reinterpret_cast<DEVNAMES*>(GlobalLock(printDialogEx.hDevNames));
			if (devNames == nullptr)
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		else
		{
			hr = E_HANDLE;
		}
	}

	// Retrieve user settings from print dialog.
	DEVMODE* devMode = nullptr;
	PCWSTR printerName = nullptr;
	if (SUCCEEDED(hr))
	{
		printerName = reinterpret_cast<PCWSTR>(devNames) + devNames->wDeviceOffset;

		if (printDialogEx.hDevMode != nullptr)
		{
			devMode = reinterpret_cast<DEVMODE*>(GlobalLock(printDialogEx.hDevMode));   // retrieve DevMode

			if (devMode)
			{
				// Must check corresponding flags in devMode->dmFields
				if ((devMode->dmFields & DM_PAPERLENGTH) && (devMode->dmFields & DM_PAPERWIDTH))
				{
					// Convert 1/10 of a millimeter DEVMODE unit to 1/96 of inch D2D unit
					m_pageHeight = devMode->dmPaperLength / 254.0f * 96.0f;
					m_pageWidth = devMode->dmPaperWidth / 254.0f * 96.0f;
				}
				else
				{
					// Use default values if the user does not specify page size.
					m_pageHeight = PAGE_HEIGHT_IN_DIPS;
					m_pageWidth = PAGE_WIDTH_IN_DIPS;
				}
			}
			else
			{
				hr = HRESULT_FROM_WIN32(GetLastError());
			}
		}
		else
		{
			hr = E_HANDLE;
		}
	}

	// Convert DEVMODE to a job print ticket stream.
	if (SUCCEEDED(hr))
	{
		hr = GetPrintTicketFromDevmode(
			printerName,
			devMode,
			devMode->dmSize + devMode->dmDriverExtra, // Size of DEVMODE in bytes, including private driver data.
			&m_jobPrintTicketStream
		);
	}

	// Create a factory for document print job.
	IPrintDocumentPackageTargetFactory* documentTargetFactory = nullptr;
	if (SUCCEEDED(hr))
	{
		hr = ::CoCreateInstance(
			__uuidof(PrintDocumentPackageTargetFactory),
			nullptr,
			CLSCTX_INPROC_SERVER,
			IID_PPV_ARGS(&documentTargetFactory)
		);
	}

	// Initialize the print subsystem and get a package target.
	if (SUCCEEDED(hr))
	{
		hr = documentTargetFactory->CreateDocumentPackageTargetForPrintJob(
			printerName,                                // printer name
			L"Direct2D desktop app printing sample",    // job name
			nullptr,                                    // job output stream; when nullptr, send to printer
			m_jobPrintTicketStream,                     // job print ticket
			&m_documentTarget                           // result IPrintDocumentPackageTarget object
		);
	}

	// Create a new print control linked to the package target.
	if (SUCCEEDED(hr))
	{
		hr = m_d2dDevice->CreatePrintControl(
			m_wicFactory,
			m_documentTarget,
			nullptr,
			&m_printControl
		);
	}

	// Create and register a print job checker.
	if (SUCCEEDED(hr))
	{
		SafeRelease(&m_printJobChecker);
		m_printJobChecker = new D2DPrintJobChecker;
		hr = (m_printJobChecker != nullptr) ? S_OK : E_OUTOFMEMORY;
	}
	if (SUCCEEDED(hr))
	{
		hr = m_printJobChecker->Initialize(m_documentTarget);
	}

	// Release resources.
	if (devMode)
	{
		GlobalUnlock(printDialogEx.hDevMode);
		devMode = nullptr;
	}
	if (devNames)
	{
		GlobalUnlock(printDialogEx.hDevNames);
		devNames = nullptr;
	}
	if (printDialogEx.hDevNames)
	{
		GlobalFree(printDialogEx.hDevNames);
	}
	if (printDialogEx.hDevMode)
	{
		GlobalFree(printDialogEx.hDevMode);
	}

	SafeRelease(&documentTargetFactory);

	return hr;
}

// Creates a print job ticket stream to define options for the next print job.
HRESULT DemoApp::GetPrintTicketFromDevmode(
	_In_ PCTSTR printerName,
	_In_reads_bytes_(devModesize) PDEVMODE devMode,
	WORD devModesize,
	_Out_ LPSTREAM* printTicketStream)
{
	HRESULT hr = S_OK;
	HPTPROVIDER provider = nullptr;

	*printTicketStream = nullptr;

	// Allocate stream for print ticket.
	hr = CreateStreamOnHGlobal(nullptr, TRUE, printTicketStream);

	if (SUCCEEDED(hr))
	{
		hr = PTOpenProvider(printerName, 1, &provider);
	}

	// Get PrintTicket from DEVMODE.
	if (SUCCEEDED(hr))
	{
		hr = PTConvertDevModeToPrintTicket(provider, devModesize, devMode, kPTJobScope, *printTicketStream);
	}

	if (FAILED(hr) && printTicketStream != nullptr)
	{
		// Release printTicketStream if fails.
		SafeRelease(printTicketStream);
	}

	if (provider)
	{
		PTCloseProvider(provider);
	}

	return hr;
}


// Commits the current print job to the printing subystem by
// closing the print control, and releases all printing-
// specific resources.
HRESULT DemoApp::FinalizePrintJob()
{
	// Send the print job to the print subsystem. (When this call
	// returns, we are safe to release printing resources.)
	HRESULT hr = m_printControl->Close();

	SafeRelease(&m_jobPrintTicketStream);
	SafeRelease(&m_printControl);

	return hr;
}

// Called whenever the application window is resized. Recreates
// the swap chain with buffers sized to the new window.
void DemoApp::OnResize()
{
}

// The window message handler for the parent window.
LRESULT CALLBACK DemoApp::ParentWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	if (message == WM_CREATE)
	{
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		DemoApp* demoApp = reinterpret_cast<DemoApp*>(createStruct->lpCreateParams);

		::SetWindowLongPtrW(
			hwnd,
			GWLP_USERDATA,
			reinterpret_cast<ULONG_PTR>(demoApp)
		);

		result = 1;
	}
	else
	{
		DemoApp* demoApp = reinterpret_cast<DemoApp*>(
			static_cast<LONG_PTR>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA))
			);

		bool wasHandled = false;

		if (demoApp)
		{
			switch (message)
			{
			case WM_CHAR:
			{
				demoApp->OnChar(static_cast<SHORT>(wParam));
			}
			result = 0;
			wasHandled = true;
			break;

			case WM_SIZE:
				if (wParam == SIZE_MAXIMIZED || wParam == SIZE_RESTORED)
				{
					demoApp->OnResize();
					result = 0;
					wasHandled = true;
				}
				break;

			case WM_VSCROLL:
			{
				demoApp->OnVScroll(wParam, lParam);
			}
			result = 0;
			wasHandled = true;
			break;

			case WM_MOUSEWHEEL:
			{
				demoApp->OnMouseWheel(wParam, lParam);
			}
			result = 0;
			wasHandled = true;
			break;

			case WM_CLOSE:
			{
				result = demoApp->OnClose();
			}
			wasHandled = true;
			break;

			case WM_DESTROY:
			{
				PostQuitMessage(0);
			}
			result = 1;
			wasHandled = true;
			break;
			}
		}

		if (!wasHandled)
		{
			result = DefWindowProc(hwnd, message, wParam, lParam);
		}
	}

	return result;
}

// The window message handler for the child window (with the D2D content).
LRESULT CALLBACK DemoApp::ChildWndProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	LRESULT result = 0;

	if (message == WM_CREATE)
	{
		LPCREATESTRUCT createStruct = reinterpret_cast<LPCREATESTRUCT>(lParam);
		DemoApp* demoApp = reinterpret_cast<DemoApp*>(createStruct->lpCreateParams);

		::SetWindowLongPtrW(
			hwnd,
			GWLP_USERDATA,
			reinterpret_cast<ULONG_PTR>(demoApp)
		);

		result = 1;
	}
	else
	{
		DemoApp* demoApp = reinterpret_cast<DemoApp*>(
			static_cast<LONG_PTR>(
				::GetWindowLongPtrW(hwnd, GWLP_USERDATA)
				)
			);

		bool wasHandled = false;

		if (demoApp)
		{
			switch (message)
			{
			case WM_DISPLAYCHANGE:
			{
				InvalidateRect(hwnd, nullptr, FALSE);
			}
			result = 0;
			wasHandled = true;
			break;

			case WM_PAINT:
			{
				PAINTSTRUCT paintStruct;
				BeginPaint(hwnd, &paintStruct);
				demoApp->OnRender();
				EndPaint(hwnd, &paintStruct);
			}
			result = 0;
			wasHandled = true;
			break;

			case WM_DESTROY:
			{
				PostQuitMessage(0);
			}
			result = 1;
			wasHandled = true;
			break;
			}
		}

		if (!wasHandled)
		{
			result = DefWindowProc(hwnd, message, wParam, lParam);
		}
	}

	return result;
}

// Creates a Direct2D bitmap from the specified file name.
HRESULT DemoApp::LoadBitmapFromFile(
	_In_ ID2D1DeviceContext* d2dContext,
	_In_ IWICImagingFactory* wicFactory,
	_In_ PCWSTR uri,
	UINT destinationWidth,
	UINT destinationHeight,
	_Outptr_ ID2D1Bitmap** bitmap
)
{
	IWICBitmapDecoder* bitmapDecoder = nullptr;
	HRESULT hr = wicFactory->CreateDecoderFromFilename(
		uri,
		nullptr,
		GENERIC_READ,
		WICDecodeMetadataCacheOnLoad,
		&bitmapDecoder
	);

	IWICBitmapFrameDecode* frameDecode = nullptr;
	if (SUCCEEDED(hr))
	{
		// Create the initial frame.
		hr = bitmapDecoder->GetFrame(0, &frameDecode);
	}

	IWICFormatConverter* formatConverter = nullptr;
	if (SUCCEEDED(hr))
	{
		// Convert the image format to 32bppPBGRA
		// (DXGI_FORMAT_B8G8R8A8_UNORM + D2D1_ALPHA_MODE_PREMULTIPLIED).
		hr = wicFactory->CreateFormatConverter(&formatConverter);
	}

	IWICBitmapScaler* bitmapScaler = nullptr;
	if (SUCCEEDED(hr))
	{
		if (destinationWidth == 0 && destinationHeight == 0)
		{
			// Don't scale the image.
			hr = formatConverter->Initialize(
				frameDecode,
				GUID_WICPixelFormat32bppPBGRA,
				WICBitmapDitherTypeNone,
				nullptr,
				0.0f,
				WICBitmapPaletteTypeMedianCut
			);
		}
		else
		{
			// If a new width or height was specified, create an
			// IWICBitmapScaler and use it to resize the image.
			UINT originalWidth;
			UINT originalHeight;
			hr = frameDecode->GetSize(&originalWidth, &originalHeight);

			if (SUCCEEDED(hr))
			{
				if (destinationWidth == 0)
				{
					FLOAT scalar = static_cast<FLOAT>(destinationHeight) / static_cast<FLOAT>(originalHeight);
					destinationWidth = static_cast<UINT>(scalar * static_cast<FLOAT>(originalWidth));
				}
				else if (destinationHeight == 0)
				{
					FLOAT scalar = static_cast<FLOAT>(destinationWidth) / static_cast<FLOAT>(originalWidth);
					destinationHeight = static_cast<UINT>(scalar * static_cast<FLOAT>(originalHeight));
				}

				hr = wicFactory->CreateBitmapScaler(&bitmapScaler);

				if (SUCCEEDED(hr))
				{
					hr = bitmapScaler->Initialize(
						frameDecode,
						destinationWidth,
						destinationHeight,
						WICBitmapInterpolationModeCubic
					);
				}

				if (SUCCEEDED(hr))
				{
					hr = formatConverter->Initialize(
						bitmapScaler,
						GUID_WICPixelFormat32bppPBGRA,
						WICBitmapDitherTypeNone,
						nullptr,
						0.f,
						WICBitmapPaletteTypeMedianCut
					);
				}
			}
		}
	}

	if (SUCCEEDED(hr))
	{
		// Create a Direct2D bitmap from the WIC bitmap.
		hr = d2dContext->CreateBitmapFromWicBitmap(
			formatConverter,
			nullptr,
			bitmap
		);
	}

	SafeRelease(&bitmapDecoder);
	SafeRelease(&frameDecode);
	SafeRelease(&formatConverter);
	SafeRelease(&bitmapScaler);

	return hr;
}

// Called when the application receives a WM_VSCROLL message is sent.
// Adjusts the application's scroll position so that subsequent renderings
// reveal a higher or lower section of the scene.
void DemoApp::OnVScroll(WPARAM wParam, LPARAM /* lParam */)
{
}

// Called when the mouse wheel is moved. Adjusts the application's
// scroll position.
void DemoApp::OnMouseWheel(WPARAM wParam, LPARAM /* lParam */)
{
}

// Resets the scroll bar to represent the current size of the
// application window and the current scroll position.
void DemoApp::ResetScrollBar()
{
}

// Close the sample window after checking print job status.
LRESULT DemoApp::OnClose()
{
	bool close = true;

	if (m_printJobChecker != nullptr)
	{
		PrintDocumentPackageStatus status = m_printJobChecker->GetStatus();
		if (status.Completion == PrintDocumentPackageCompletion_InProgress)
		{
			int selection = MessageBox(
				0,
				L"Print job still in progress.\nYES to force to exit;\nNO to exit after print job is complete;\nCANCEL to return to sample.",
				L"Sample exit error",
				MB_YESNOCANCEL | MB_ICONSTOP
			);
			switch (selection)
			{
			case IDYES:
				// Force to exit.
				break;

			case IDNO:
				// Exit after print job is complete.
				m_printJobChecker->WaitForCompletion();
				break;

			case IDCANCEL:
				// Return to sample.
				close = false;
				break;

			default:
				close = false;
				break;
			}
		}
	}

	if (close)
	{
		DestroyWindow(m_parentHwnd);
	}

	return close ? 0 : 1;
}