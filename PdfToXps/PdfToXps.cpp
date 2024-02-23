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

// XPS
#include <initguid.h>
#include <xpsobjectmodel_1.h>
#include <DocumentTarget.h>

// SHCreateStreamOnFile
#include "Shlwapi.h"
#pragma comment(lib, "shlwapi.lib")

using namespace winrt::Windows::Foundation;
using namespace winrt::Windows::Storage;
using namespace winrt::Windows::Data::Pdf;

struct PrintJob
{
	IStream* outStream;
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

winrt::fire_and_forget main_async()
{
	try
	{
		winrt::com_ptr<IStream> stream;
		winrt::check_hresult(::SHCreateStreamOnFile(L"out.xps", STGM_CREATE | STGM_WRITE, stream.put()));

		// file
		IStorageFile file = co_await StorageFile::GetFileFromPathAsync(L"C:\\dev\\gitroot\\D2DPrintSample\\D2DPrintSample\\example.pdf");
		PdfDocument pdfDoc = co_await PdfDocument::LoadFromFileAsync(file);

		DemoApp app;
		app.OnPrint({ stream.get(), pdfDoc });
	}
	catch (...)
	{
		__debugbreak();
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

// https://stackoverflow.com/a/77972286/138200
struct CTarget : winrt::implements<CTarget, IPrintDocumentPackageTarget, IXpsDocumentPackageTarget>
{
	winrt::com_ptr<IXpsOMObjectFactory> _factory;
	IStream* pStream;

	CTarget(IStream* pStream) 
		: pStream(pStream), _factory(winrt::create_instance<IXpsOMObjectFactory>(CLSID_XpsOMObjectFactory))
	{
		// nop
	}

	// IPrintDocumentPackageTarget
	STDMETHODIMP GetPackageTargetTypes(UINT32* targetCount, GUID** targetTypes)
	{
		if (!targetCount || !targetTypes) return E_INVALIDARG;
		*targetTypes = (GUID*)CoTaskMemAlloc(sizeof(GUID));
		if (!*targetTypes)
		{
			*targetCount = 0;
			return E_OUTOFMEMORY;
		}

		*targetCount = 1;
		**targetTypes = ID_DOCUMENTPACKAGETARGET_MSXPS;
		return S_OK;
	}

	STDMETHODIMP GetPackageTarget(REFGUID guidTargetType, REFIID riid, void** ppvTarget)
	{
		if (guidTargetType == ID_DOCUMENTPACKAGETARGET_MSXPS)
			return QueryInterface(riid, ppvTarget);

		return E_FAIL;
	}

	STDMETHODIMP Cancel()
	{
		return S_OK;
	}

	// IXpsDocumentPackageTarget
	STDMETHODIMP GetXpsOMPackageWriter(IOpcPartUri* documentSequencePartName, IOpcPartUri* discardControlPartName, IXpsOMPackageWriter** packageWriter)
	{
		return _factory->CreatePackageWriterOnStream(pStream, FALSE, XPS_INTERLEAVING_OFF, 
			documentSequencePartName, nullptr, nullptr, nullptr, discardControlPartName, packageWriter);
	}

	STDMETHODIMP GetXpsOMFactory(IXpsOMObjectFactory** xpsFactory)
	{
		*xpsFactory = _factory.get();
		return S_OK;
	}

	STDMETHODIMP GetXpsType(XPS_DOCUMENT_TYPE* documentType)
	{
		if (!documentType) return E_INVALIDARG;
		*documentType = XPS_DOCUMENT_TYPE_XPS;
		return S_OK;
	}
};

void DemoApp::OnPrint(const PrintJob& job)
{
	// Initialize the job
	auto xpsTarget{ winrt::make<CTarget>(job.outStream) };
	winrt::com_ptr<ID2D1PrintControl> printControl;
	winrt::check_hresult(d2dDevice->CreatePrintControl(pWic.get(), xpsTarget.get(), nullptr, printControl.put()));

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

	winrt::check_hresult(printControl->Close());
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