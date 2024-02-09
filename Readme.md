# D2D PDF Printer

Example of using [Windows.Data.Pdf](https://learn.microsoft.com/en-us/uwp/api/windows.data.pdf?view=winrt-22621) from C++/winrt or C# to print a PDF file.

No third party apps required - no additional licenses.  Only Windows.

PDF's are printed as bitmaps.  Text is preserved.  Minimal testing done - demo only, not production ready.

## Key steps

1. Reference/initialize winrt (e.g.: `winrt::init_apartment(winrt::apartment_type::single_threaded);`)
1. Decide what printer to use (`PrintDlgEx` or similar) and create a print ticket
1. Open the PDF with `PdfDocument::LoadFromFileAsync(file)`
1. Acquire system resources (D3D, DXGI, D2D, WIC)
1. Create a print job (`IPrintDocumentPackageTargetFactory::CreateDocumentPackageTargetForPrintJob` and `ID2D1PrintControl`)
1. Create a PDF renderer (`PdfCreateRenderer`)
1. Loop over pages, render to a command list, `RenderPageToDeviceContext`, and add the command list as a page to the `ID2D1PrintControl`.
1. End the document with `ID2D1PrintControl::Close`
1. Keep alive for a bit, pumping messages (seemingly only needed for printing to virtual printers like "Acrobat PDF Converter")

## Examples
- [D2DPrintSample](D2DPrintSample)<br/>C++ Sample based on https://github.com/microsoft/Windows-classic-samples/blob/main/Samples/D2DPrintingFromDesktopApps/cpp/D2DPrintingFromDesktopApps.cpp
- [PrintPdf](PrintPdf)<br/>Minimal C++ sample reduced from D2DPrintSample
- [PrintPdfManaged](PrintPdfManaged)<br/>C#/net7 port of PrintPdf

## Future possibilites
- WPF viewer control
- NuGet wrapper