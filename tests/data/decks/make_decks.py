#!/usr/bin/env python3
"""Writes the sample PowerPoint decks used by tst_presentations.

Run from anywhere: python3 make_decks.py  (writes next to this script).
Only the standard library is used, so the decks can be regenerated anywhere.

Each slide has its own solid background colour, so a test can check the
order of the imported pictures from one corner pixel. The slide files inside
the zip are deliberately named out of order: the show order comes from
ppt/presentation.xml, as in decks that were reordered in PowerPoint.
"""

import os
import struct
import zipfile
import zlib

HERE = os.path.dirname(os.path.abspath(__file__))

NS = ('xmlns:a="http://schemas.openxmlformats.org/drawingml/2006/main" '
      'xmlns:r="http://schemas.openxmlformats.org/officeDocument/2006/relationships" '
      'xmlns:p="http://schemas.openxmlformats.org/presentationml/2006/main"')
REL = "http://schemas.openxmlformats.org/officeDocument/2006/relationships"
PKG_REL = "http://schemas.openxmlformats.org/package/2006/relationships"


def png(width, height, rgb):
    """A solid-colour PNG."""
    raw = b"".join(b"\x00" + bytes(rgb) * width for _ in range(height))

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xFFFFFFFF)

    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def rels(entries):
    body = "".join(f'<Relationship Id="{i}" Type="{REL}/{t}" Target="{target}"/>' for i, t, target in entries)
    return f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><Relationships xmlns="{PKG_REL}">{body}</Relationships>'


THEME = f'''<?xml version="1.0" encoding="UTF-8" standalone="yes"?>
<a:theme {NS} name="Lumen test"><a:themeElements>
<a:clrScheme name="Lumen"><a:dk1><a:srgbClr val="000000"/></a:dk1><a:lt1><a:srgbClr val="FFFFFF"/></a:lt1>
<a:dk2><a:srgbClr val="1F1F1F"/></a:dk2><a:lt2><a:srgbClr val="EEEEEE"/></a:lt2>
<a:accent1><a:srgbClr val="4472C4"/></a:accent1><a:accent2><a:srgbClr val="ED7D31"/></a:accent2>
<a:accent3><a:srgbClr val="A5A5A5"/></a:accent3><a:accent4><a:srgbClr val="FFC000"/></a:accent4>
<a:accent5><a:srgbClr val="5B9BD5"/></a:accent5><a:accent6><a:srgbClr val="70AD47"/></a:accent6>
<a:hlink><a:srgbClr val="0563C1"/></a:hlink><a:folHlink><a:srgbClr val="954F72"/></a:folHlink></a:clrScheme>
<a:fontScheme name="Lumen"><a:majorFont><a:latin typeface="DejaVu Sans"/><a:ea typeface=""/><a:cs typeface=""/></a:majorFont>
<a:minorFont><a:latin typeface="DejaVu Sans"/><a:ea typeface=""/><a:cs typeface=""/></a:minorFont></a:fontScheme>
<a:fmtScheme name="Lumen"><a:fillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:fillStyleLst>
<a:lnStyleLst><a:ln w="6350"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="12700"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln><a:ln w="19050"><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:ln></a:lnStyleLst>
<a:effectStyleLst><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle><a:effectStyle><a:effectLst/></a:effectStyle></a:effectStyleLst>
<a:bgFillStyleLst><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill><a:solidFill><a:schemeClr val="phClr"/></a:solidFill></a:bgFillStyleLst></a:fmtScheme>
</a:themeElements></a:theme>'''


def placeholder(id_, name, ph, frame=None, text_xml=""):
    xfrm = ""
    if frame:
        x, y, cx, cy = frame
        xfrm = f'<a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
    return (f'<p:sp><p:nvSpPr><p:cNvPr id="{id_}" name="{name}"/><p:cNvSpPr><a:spLocks noGrp="1"/></p:cNvSpPr>'
            f'<p:nvPr>{ph}</p:nvPr></p:nvSpPr><p:spPr>{xfrm}</p:spPr>'
            f'<p:txBody><a:bodyPr/><a:lstStyle/>{text_xml or "<a:p><a:endParaRPr lang=\"en-US\"/></a:p>"}</p:txBody></p:sp>')


def master(cx, cy):
    title = placeholder(2, "Title", '<p:ph type="title"/>', (cx // 12, cy // 20, cx * 10 // 12, cy // 5))
    body = placeholder(3, "Body", '<p:ph type="body" idx="1"/>', (cx // 12, cy * 3 // 10, cx * 10 // 12, cy * 6 // 10))
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:sldMaster {NS}><p:cSld>'
            '<p:bg><p:bgRef idx="1001"><a:schemeClr val="bg1"/></p:bgRef></p:bg>'
            '<p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>'
            f'{title}{body}</p:spTree></p:cSld>'
            '<p:clrMap bg1="lt1" tx1="dk1" bg2="lt2" tx2="dk2" accent1="accent1" accent2="accent2" accent3="accent3" '
            'accent4="accent4" accent5="accent5" accent6="accent6" hlink="hlink" folHlink="folHlink"/>'
            '<p:sldLayoutIdLst><p:sldLayoutId id="2147483649" r:id="rId1"/></p:sldLayoutIdLst>'
            '<p:txStyles><p:titleStyle><a:lvl1pPr algn="ctr"><a:defRPr sz="4400"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill>'
            '<a:latin typeface="+mj-lt"/></a:defRPr></a:lvl1pPr></p:titleStyle>'
            '<p:bodyStyle><a:lvl1pPr marL="228600"><a:buChar char="&#8226;"/><a:defRPr sz="2800"><a:solidFill><a:schemeClr val="tx1"/></a:solidFill>'
            '<a:latin typeface="+mn-lt"/></a:defRPr></a:lvl1pPr></p:bodyStyle>'
            '<p:otherStyle><a:lvl1pPr><a:defRPr sz="1800"/></a:lvl1pPr></p:otherStyle></p:txStyles></p:sldMaster>')


def layout():
    # Placeholders without positions: they take the master's.
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:sldLayout {NS} type="obj" preserve="1">'
            '<p:cSld name="Title and Content"><p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/>'
            '</p:nvGrpSpPr><p:grpSpPr/>'
            f'{placeholder(2, "Title", "<p:ph type=\"title\"/>")}{placeholder(3, "Content", "<p:ph idx=\"1\"/>")}'
            '</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sldLayout>')


def paragraph(text):
    return f'<a:p><a:r><a:rPr lang="en-US" dirty="0"/><a:t>{text}</a:t></a:r></a:p>'


def slide(colour, title, bullets=(), picture=None, hidden=False):
    shapes = placeholder(2, "Title", '<p:ph type="title"/>', None, paragraph(title))
    if bullets:
        shapes += placeholder(3, "Content", '<p:ph idx="1"/>', None, "".join(paragraph(b) for b in bullets))
    if picture:
        x, y, cx, cy = picture
        shapes += (f'<p:pic><p:nvPicPr><p:cNvPr id="4" name="Picture"/><p:cNvPicPr/><p:nvPr/></p:nvPicPr>'
                   f'<p:blipFill><a:blip r:embed="rId2"/><a:stretch><a:fillRect/></a:stretch></p:blipFill>'
                   f'<p:spPr><a:xfrm><a:off x="{x}" y="{y}"/><a:ext cx="{cx}" cy="{cy}"/></a:xfrm>'
                   f'<a:prstGeom prst="rect"><a:avLst/></a:prstGeom></p:spPr></p:pic>')
    show = ' show="0"' if hidden else ""
    return (f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:sld {NS}{show}><p:cSld>'
            f'<p:bg><p:bgPr><a:solidFill><a:srgbClr val="{colour}"/></a:solidFill><a:effectLst/></p:bgPr></p:bg>'
            '<p:spTree><p:nvGrpSpPr><p:cNvPr id="1" name=""/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr><p:grpSpPr/>'
            f'{shapes}</p:spTree></p:cSld><p:clrMapOvr><a:masterClrMapping/></p:clrMapOvr></p:sld>')


def deck(name, cx, cy, slides):
    """slides: list of (file number, slide xml, has picture) in show order."""
    files = {}
    types = ['<Default Extension="rels" ContentType="application/vnd.openxmlformats-package.relationships+xml"/>',
             '<Default Extension="xml" ContentType="application/xml"/>',
             '<Default Extension="png" ContentType="image/png"/>',
             '<Override PartName="/ppt/presentation.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml"/>',
             '<Override PartName="/ppt/slideMasters/slideMaster1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideMaster+xml"/>',
             '<Override PartName="/ppt/slideLayouts/slideLayout1.xml" ContentType="application/vnd.openxmlformats-officedocument.presentationml.slideLayout+xml"/>',
             '<Override PartName="/ppt/theme/theme1.xml" ContentType="application/vnd.openxmlformats-officedocument.theme+xml"/>']
    pres_rels = [("rId1", "slideMaster", "slideMasters/slideMaster1.xml"), ("rId2", "theme", "theme/theme1.xml")]
    ids = []
    for order, (number, xml, has_picture) in enumerate(slides):
        rid = f"rId{10 + order}"
        pres_rels.append((rid, "slide", f"slides/slide{number}.xml"))
        ids.append(f'<p:sldId id="{256 + order}" r:id="{rid}"/>')
        files[f"ppt/slides/slide{number}.xml"] = xml
        slide_rels = [("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml")]
        if has_picture:
            slide_rels.append(("rId2", "image", f"../media/image{number}.png"))
            files[f"ppt/media/image{number}.png"] = png(64, 36, (0xFF, 0xD7, 0x00))
        files[f"ppt/slides/_rels/slide{number}.xml.rels"] = rels(slide_rels)
        types.append(f'<Override PartName="/ppt/slides/slide{number}.xml" '
                     'ContentType="application/vnd.openxmlformats-officedocument.presentationml.slide+xml"/>')
    files["[Content_Types].xml"] = ('<?xml version="1.0" encoding="UTF-8" standalone="yes"?>'
                                    '<Types xmlns="http://schemas.openxmlformats.org/package/2006/content-types">'
                                    + "".join(types) + "</Types>")
    files["_rels/.rels"] = rels([("rId1", "officeDocument", "ppt/presentation.xml")])
    files["ppt/presentation.xml"] = (
        f'<?xml version="1.0" encoding="UTF-8" standalone="yes"?><p:presentation {NS}>'
        '<p:sldMasterIdLst><p:sldMasterId id="2147483648" r:id="rId1"/></p:sldMasterIdLst>'
        f'<p:sldIdLst>{"".join(ids)}</p:sldIdLst><p:sldSz cx="{cx}" cy="{cy}"/><p:notesSz cx="6858000" cy="9144000"/>'
        '</p:presentation>')
    files["ppt/_rels/presentation.xml.rels"] = rels(pres_rels)
    files["ppt/slideMasters/slideMaster1.xml"] = master(cx, cy)
    files["ppt/slideMasters/_rels/slideMaster1.xml.rels"] = rels(
        [("rId1", "slideLayout", "../slideLayouts/slideLayout1.xml"), ("rId2", "theme", "../theme/theme1.xml")])
    files["ppt/slideLayouts/slideLayout1.xml"] = layout()
    files["ppt/slideLayouts/_rels/slideLayout1.xml.rels"] = rels(
        [("rId1", "slideMaster", "../slideMasters/slideMaster1.xml")])
    files["ppt/theme/theme1.xml"] = THEME

    path = os.path.join(HERE, name)
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        # [Content_Types].xml first, as Office writes it.
        z.writestr("[Content_Types].xml", files.pop("[Content_Types].xml"))
        for part in sorted(files):
            data = files[part]
            z.writestr(zipfile.ZipInfo(part, (2024, 1, 1, 0, 0, 0)), data, zipfile.ZIP_DEFLATED)
    print("wrote", path)


# 16:9, six slides in show order; file numbers scrambled; the fifth hidden.
# Colours (show order): red, green, blue, (hidden magenta), cyan... see the test.
W169, H169 = 12192000, 6858000
deck("order-16x9.pptx", W169, H169, [
    (4, slide("C62828", "Welcome", ["Service starts at 10:00", "Please silence your phone"]), False),
    (1, slide("2E7D32", "Announcements", ["Youth camp: sign up by Sunday"], picture=(W169 * 6 // 10, H169 * 6 // 10, W169 // 4, H169 // 4)), True),
    (6, slide("1565C0", "Sermon: Grace", ["Ephesians 2:8-9", "Saved by grace", "Through faith"]), False),
    (2, slide("6A1B9A", "Offering"), False),
    (5, slide("AD1457", "Hidden notes for the team", ["Not shown"], hidden=True), False),
    (3, slide("00838F", "Thank you for coming"), False),
])

# 4:3, three slides.
W43, H43 = 9144000, 6858000
deck("classic-4x3.pptx", W43, H43, [
    (1, slide("EF6C00", "Prayer requests"), False),
    (2, slide("37474F", "Small groups", ["Tuesday 7pm", "Thursday 7pm"]), False),
    (3, slide("558B2F", "Benediction"), False),
])
