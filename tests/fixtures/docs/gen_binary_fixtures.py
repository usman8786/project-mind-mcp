#!/usr/bin/env python3
"""Generate sample.docx / sample.pdf fixtures for extract_docs tests."""
import os
import zipfile

BASE = os.path.dirname(os.path.abspath(__file__))


def main() -> None:
    xml = (
        '<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
        '<w:document xmlns:w="http://schemas.openxmlformats.org/wordprocessingml/2006/main">'
        "<w:body>"
        "<w:p><w:r><w:t>Docx Title</w:t></w:r></w:p>"
        "<w:p><w:r><w:t>Docx body mentions authenticate_user.</w:t></w:r></w:p>"
        "</w:body></w:document>"
    )
    docx = os.path.join(BASE, "sample.docx")
    with zipfile.ZipFile(docx, "w", compression=zipfile.ZIP_DEFLATED) as z:
        z.writestr(
            "[Content_Types].xml",
            '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
            '<Default Extension="xml" ContentType="application/xml"/>'
            '<Override PartName="/word/document.xml" '
            'ContentType="application/vnd.openxmlformats-officedocument.wordprocessingml.document.main+xml"/>'
            "</Types>",
        )
        z.writestr("word/document.xml", xml)

    pdf = os.path.join(BASE, "sample.pdf")
    content = b"""%PDF-1.1
1 0 obj<< /Type /Catalog /Pages 2 0 R >>endobj
2 0 obj<< /Type /Pages /Kids [3 0 R] /Count 1 >>endobj
3 0 obj<< /Type /Page /Parent 2 0 R /MediaBox [0 0 300 144] /Contents 4 0 R /Resources<< /Font<< /F1 5 0 R >> >> >>endobj
4 0 obj<< /Length 55 >>stream
BT /F1 12 Tf 72 72 Td (PdfGoldenText) Tj ET
endstream
endobj
5 0 obj<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>endobj
trailer<< /Root 1 0 R >>
%%EOF
"""
    with open(pdf, "wb") as f:
        f.write(content)
    print("wrote", docx, os.path.getsize(docx), "bytes")
    print("wrote", pdf, os.path.getsize(pdf), "bytes")


if __name__ == "__main__":
    main()
