using System;
using System.IO;
using System.Printing;
using System.Security;
using System.Windows;
using System.Windows.Controls;
using Windows.Data.Pdf;
using Windows.Devices.HumanInterfaceDevice;
using Windows.Devices.I2c;
using Windows.Storage;
using Windows.Win32;
using Windows.Win32.Foundation;
using Windows.Win32.System.Com;
using Windows.Win32.Graphics.Direct2D;
using Windows.Win32.Graphics.Direct3D;
using Windows.Win32.Graphics.Direct3D11;
using Windows.Win32.Graphics.Dxgi;
using Windows.Win32.Graphics.Imaging.D2D;
using Windows.Win32.Storage.Xps.Printing;
using Windows.Win32.System.WinRT.Pdf;
using System.Runtime.InteropServices;
using System.Net.NetworkInformation;
using Windows.Win32.Graphics.Direct2D.Common;

namespace PrintPdfManaged
{
    internal class Program
    {
        [STAThread]
        static async Task Main(string[] args)
        {
            var printer = LocalPrintServer.GetDefaultPrintQueue();
            var ticket = printer.UserPrintTicket.GetXmlStream().ToArray();

            var pdfFile = await StorageFile.GetFileFromPathAsync("C:\\dev\\gitroot\\D2DPrintSample\\D2DPrintSample\\example.pdf");
            var pdfDoc = await PdfDocument.LoadFromFileAsync(pdfFile);

            var pdfPrinter = new PdfPrinter();
            pdfPrinter.Print(printer.Name, "Example job", ticket, pdfDoc);

            var app = new Application();
            app.Run();
        }
    }

    class PdfPrinter
    {
        private ID3D11Device d3dDevice;
        private IDXGIDevice dxgiDevice;
        private ID2D1Factory1 d2dFactory;
        private IWICImagingFactory2 pWic;
        private ID2D1Device d2dDevice;
        private ID2D1DeviceContext d2dContextForPrint;

        public unsafe PdfPrinter()
        {
            // d3d
            PInvoke.D3D11CreateDevice(null, D3D_DRIVER_TYPE.D3D_DRIVER_TYPE_HARDWARE, HMODULE.Null,
                D3D11_CREATE_DEVICE_FLAG.D3D11_CREATE_DEVICE_BGRA_SUPPORT,
                null, 0, 7 /* D3D11_SDK_VERSION */, out d3dDevice, null, out var d3dContext)
                .ThrowOnFailure();
            dxgiDevice = (IDXGIDevice)d3dDevice;

            // d2d
            var options = new D2D1_FACTORY_OPTIONS();
#if DEBUG
            options.debugLevel = D2D1_DEBUG_LEVEL.D2D1_DEBUG_LEVEL_INFORMATION;
#endif
            PInvoke.D2D1CreateFactory(D2D1_FACTORY_TYPE.D2D1_FACTORY_TYPE_MULTI_THREADED, typeof(ID2D1Factory1).GUID, options, out var od2dFactory).ThrowOnFailure();
            d2dFactory = (ID2D1Factory1)od2dFactory;
            d2dFactory.CreateDevice(dxgiDevice, out d2dDevice);
            d2dDevice.CreateDeviceContext(D2D1_DEVICE_CONTEXT_OPTIONS.D2D1_DEVICE_CONTEXT_OPTIONS_NONE, out d2dContextForPrint);

            // wic
            pWic = (IWICImagingFactory2)Activator.CreateInstance(Type.GetTypeFromCLSID(PInvoke.CLSID_WICImagingFactory)!)!;
        }

        public unsafe void Print(string printerName, string jobName, byte[] ticket, PdfDocument pdfDoc)
        {
            // Initialize the job
            ID2D1PrintControl printControl;
            {
                // Create a factory for document print job.
                var documentTargetFactory = (IPrintDocumentPackageTargetFactory)new PrintDocumentPackageTargetFactory();
                IPrintDocumentPackageTarget docTarget;
                documentTargetFactory.CreateDocumentPackageTargetForPrintJob(
                    printerName, jobName, null, ArrayToIStream(ticket), out docTarget);

                // Create a new print control linked to the package target.
                d2dDevice.CreatePrintControl(pWic, docTarget, null, out printControl);
            }

            // Open the PDF Document
            PInvoke.PdfCreateRenderer(dxgiDevice, out var pPdfRendererNative).ThrowOnFailure();
            var renderParams = new PDF_RENDER_PARAMS();

            // Write pages
            for (uint pageIndex = 0; pageIndex < pdfDoc.PageCount; pageIndex++)
            {
                var pdfPage = pdfDoc.GetPage(pageIndex);

                d2dContextForPrint.CreateCommandList(out var commandList);
                d2dContextForPrint.SetTarget(commandList);

                d2dContextForPrint.BeginDraw();
                pPdfRendererNative.RenderPageToDeviceContext(pdfPage, d2dContextForPrint, &renderParams);
                d2dContextForPrint.EndDraw();

                commandList.Close();
                printControl.AddPage(commandList, pdfPage.Size.AsD2dSizeF(), null);
            }

            printControl.Close();
        }

        private unsafe IStream ArrayToIStream(byte[] data)
        {
            PInvoke.CreateStreamOnHGlobal((HGLOBAL)null, true, out var stm).ThrowOnFailure();
            uint sz = (uint)data.Length;
            stm.SetSize(sz);
            fixed (byte* pData = data) 
            {
                uint cbWritten;
                stm.Write(pData, sz, &cbWritten).ThrowOnFailure();
                if (cbWritten != sz) throw new InvalidOperationException();
            }
            return stm;
        }
    }

    internal static class D2DExtensions
    {
        public static D2D_SIZE_F AsD2dSizeF(this Windows.Foundation.Size sz)
        {
            return new()
            {
                width = (float)sz.Width,
                height = (float)sz.Height,
            };
        }
    }
}