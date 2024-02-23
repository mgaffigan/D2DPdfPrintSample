// PdfToXps.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <string>
#include <iostream>
#define WINVER 0x0A00 // Windows 10
#define _WIN32_WINNT 0x0A00 // Windows 10

#include <wil\stl.h>
#include <wil\resource.h>
#include <wil\win32_helpers.h>

// WinRT
#include <winrt\base.h>
#include <winrt\windows.foundation.h>
#include <winrt\windows.storage.h>
#include <winrt\windows.data.pdf.h>
#include <windows.data.pdf.interop.h>
#pragma comment(lib, "windowsapp")

// d3d
#include <d3d11.h>
#pragma comment(lib, "d3d11")

// d2d
#include <d2d1_1.h>
#pragma comment(lib, "d2d1")

// wic
#include <wincodec.h>

// Print tickets
#include <DocumentTarget.h>
#include <Prntvpt.h>
#pragma comment(lib, "prntvpt.lib")

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Data::Pdf;

struct PrintJob
{
	const wchar_t* printerName;
	const wchar_t* jobName;
	IStream* printTicket;

	IPdfDocument file;
};

struct DemoApp
{
	winrt::com_ptr<ID3D11Device> d3dDevice;
	winrt::com_ptr<IDXGIDevice> dxgiDevice;
	winrt::com_ptr<ID2D1Factory1> d2dFactory;
	winrt::com_ptr<IWICImagingFactory2> pWic;
	winrt::com_ptr<ID2D1Device> d2dDevice;
	winrt::com_ptr<ID2D1DeviceContext> d2dContextForPrint;

	DemoApp();

	void OnPrint(const PrintJob& job);
};

typedef wil::unique_any<HPTPROVIDER, decltype(&::PTCloseProvider), ::PTCloseProvider> unique_hptprovider;
typedef wil::unique_any<HANDLE, decltype(&::ClosePrinter), ::ClosePrinter> unique_hprinter;

std::wstring GetDefaultPrinterSafe()
{
	std::wstring result;
	winrt::check_hresult(wil::AdaptFixedSizeToAllocatedResult<std::wstring>(
		result, [&](_Out_writes_(valueLength) PWSTR value, size_t valueLength, _Out_ size_t* valueLengthNeededWithNul) -> HRESULT {
			DWORD cch = static_cast<DWORD>(valueLength);
			auto err = ::GetDefaultPrinterW(value, &cch);
			*valueLengthNeededWithNul = cch;
			RETURN_LAST_ERROR_IF(err == 0);
			return S_OK;
		}));
	return result;
}

template <typename TInfo, int TInfoNo>
wil::unique_hlocal_ptr<TInfo> GetPrinterSafe(HANDLE hPrinter)
{
	DWORD cb = 0;
	GetPrinter(hPrinter, TInfoNo, nullptr, cb, &cb);
	wil::unique_hlocal_ptr<TInfo> result{ static_cast<TInfo*>(LocalAlloc(0, cb)) };
	THROW_HR_IF(E_OUTOFMEMORY, result.get() == 0);
	THROW_LAST_ERROR_IF(!GetPrinter(hPrinter, TInfoNo, (LPBYTE)result.get(), cb, &cb));
	return result;
}

winrt::com_ptr<IStream> GetPrintTicketForDevMode(LPWSTR printerName, DEVMODE* devMode)
{
	unique_hptprovider provider;
	winrt::check_hresult(PTOpenProvider(printerName, 1, &provider));

	// Get PrintTicket from DEVMODE.
	winrt::com_ptr<IStream> ticket;
	winrt::check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, ticket.put()));
	winrt::check_hresult(PTConvertDevModeToPrintTicket(provider.get(),
		devMode->dmSize + devMode->dmDriverExtra, devMode, kPTJobScope, ticket.get()));

	return ticket;
}

winrt::fire_and_forget main_async()
{
	try
	{
		auto printerName = GetDefaultPrinterSafe();
		unique_hprinter printer;
		THROW_LAST_ERROR_IF(!OpenPrinterW(const_cast<LPWSTR>(printerName.c_str()), &printer, nullptr));
		auto hDevMode = GetPrinterSafe<PRINTER_INFO_8, 8>(printer.get());
		auto ticket = GetPrintTicketForDevMode(const_cast<LPWSTR>(printerName.c_str()), hDevMode.get()->pDevMode);

		// file
		IStorageFile file = co_await StorageFile::GetFileFromPathAsync(L"C:\\dev\\gitroot\\D2DPrintSample\\D2DPrintSample\\example.pdf");
		PdfDocument pdfDoc = co_await PdfDocument::LoadFromFileAsync(file);

		DemoApp app;
		app.OnPrint({ printerName.c_str(), L"Example job", ticket.get(), pdfDoc });
	}
	catch (...)
	{
		;
	}
}

int main()
{
	winrt::init_apartment(winrt::apartment_type::single_threaded);

	main_async();

	MSG msg;
	while (GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}

	return 0;
}

DemoApp::DemoApp()
{
	// d3d
	winrt::check_hresult(::D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, 0, D3D11_CREATE_DEVICE_BGRA_SUPPORT,
		nullptr, 0, D3D11_SDK_VERSION, d3dDevice.put(), 0, nullptr));
	dxgiDevice = d3dDevice.as<IDXGIDevice>();

	// d2d
	D2D1_FACTORY_OPTIONS options = {};
#if defined(_DEBUG)
	options.debugLevel = D2D1_DEBUG_LEVEL_INFORMATION;
#endif
	winrt::check_hresult(::D2D1CreateFactory(D2D1_FACTORY_TYPE_MULTI_THREADED, options, d2dFactory.put()));
	winrt::check_hresult(d2dFactory->CreateDevice(dxgiDevice.get(), d2dDevice.put()));
	winrt::check_hresult(d2dDevice->CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS_NONE, d2dContextForPrint.put()));

	// wic
	pWic = winrt::create_instance<IWICImagingFactory2>(CLSID_WICImagingFactory);
}

void DemoApp::OnPrint(const PrintJob& job)
{
	// Initialize the job
	winrt::com_ptr<ID2D1PrintControl> printControl;
	{
		// Create a factory for document print job.
		auto documentTargetFactory = winrt::create_instance<IPrintDocumentPackageTargetFactory>(__uuidof(PrintDocumentPackageTargetFactory));
		winrt::com_ptr<IPrintDocumentPackageTarget> docTarget;
		winrt::check_hresult(documentTargetFactory->CreateDocumentPackageTargetForPrintJob(
			job.printerName, job.jobName, nullptr, job.printTicket, docTarget.put()));

		// Create a new print control linked to the package target.
		winrt::check_hresult(d2dDevice->CreatePrintControl(pWic.get(), docTarget.get(), nullptr, printControl.put()));
	}

	// Open the PDF Document
	winrt::com_ptr<IPdfRendererNative> pPdfRendererNative;
	winrt::check_hresult(PdfCreateRenderer(dxgiDevice.get(), pPdfRendererNative.put()));
	PDF_RENDER_PARAMS renderParams = {}; // You can set additional parameters if needed.

	// Write pages
	for (uint32_t pageIndex = 0; pageIndex < job.file.PageCount(); pageIndex++)
	{
		IPdfPage pdfPage = job.file.GetPage(pageIndex);
		auto pdfSize = pdfPage.Size();

		winrt::com_ptr<ID2D1CommandList> commandList;
		winrt::check_hresult(d2dContextForPrint->CreateCommandList(commandList.put()));
		d2dContextForPrint->SetTarget(commandList.get());

		d2dContextForPrint->BeginDraw();
		winrt::check_hresult(pPdfRendererNative->RenderPageToDeviceContext(winrt::get_unknown(pdfPage), d2dContextForPrint.get(), &renderParams));
		d2dContextForPrint->EndDraw();

		winrt::check_hresult(commandList->Close());
		winrt::check_hresult(printControl->AddPage(commandList.get(), D2D1::SizeF(pdfSize.Width, pdfSize.Height), nullptr));
	}

	printControl->Close();
}


/*
	// Bring up Print Dialog and receive user print settings.
	PRINTDLGEX printDialogEx = { 0 };
	printDialogEx.lStructSize = sizeof(PRINTDLGEX);
	printDialogEx.Flags = PD_HIDEPRINTTOFILE | PD_NOPAGENUMS | PD_NOSELECTION | PD_NOCURRENTPAGE | PD_USEDEVMODECOPIESANDCOLLATE;
	printDialogEx.hwndOwner = HWND_DESKTOP;
	printDialogEx.nStartPage = START_PAGE_GENERAL;

	winrt::check_hresult(PrintDlgEx(&printDialogEx));
	wil::unique_hglobal devmode_result(printDialogEx.hDevMode);
	wil::unique_hglobal devnames_result(printDialogEx.hDevNames);

	// Retrieve DEVNAMES from print dialog.
	wil::unique_hglobal_locked devnames_lock(devnames_result.get());
	DEVNAMES* devNames = reinterpret_cast<DEVNAMES*>(devnames_lock.get());
	if (devNames == nullptr)
	{
		winrt::throw_last_error();
	}

	// Retrieve user settings from print dialog.
	PCWSTR printerName = reinterpret_cast<PCWSTR>(devNames) + devNames->wDeviceOffset;
	wil::unique_hglobal_locked devmode_lock(printDialogEx.hDevMode);
	DEVMODE* devMode = reinterpret_cast<DEVMODE*>(devmode_lock.get());

	// Convert DEVMODE to a job print ticket stream.
	winrt::com_ptr<IStream> jobPrintTicketStream;
	winrt::check_hresult(CreateStreamOnHGlobal(nullptr, TRUE, jobPrintTicketStream.put()));
	{
		unique_hptprovider provider;
		winrt::check_hresult(PTOpenProvider(printerName, 1, &provider));

		// Get PrintTicket from DEVMODE.
		winrt::check_hresult(PTConvertDevModeToPrintTicket(provider.get(),
			devMode->dmSize + devMode->dmDriverExtra, devMode, kPTJobScope, jobPrintTicketStream.get()));
	}
	*/