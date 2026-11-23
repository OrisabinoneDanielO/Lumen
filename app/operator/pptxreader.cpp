#include "pptxreader.h"

#include <QDir>
#include <QDomDocument>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <private/qzipreader_p.h>

#include <algorithm>
#include <functional>
#include <initializer_list>
#include <map>

namespace lumen::app {

namespace {

constexpr double emuPerPoint = 12700.0;

// Element names without their namespace prefix ("p:sp" -> "sp"). Files from
// PowerPoint, LibreOffice and other tools all use the usual prefixes, but
// matching on the local name does not depend on that.
QString localName(const QDomElement &e)
{
    const QString tag = e.tagName();
    const int colon = tag.indexOf(u':');
    return colon < 0 ? tag : tag.mid(colon + 1);
}

QDomElement child(const QDomElement &e, QStringView name)
{
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement()) {
        if (localName(c) == name)
            return c;
    }
    return {};
}

QDomElement child(const QDomElement &e, QLatin1StringView name)
{
    const QString text(name);
    return child(e, QStringView(text));
}

QDomElement path(QDomElement e, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (e.isNull())
            break;
        e = child(e, QLatin1String(name));
    }
    return e;
}

QList<QDomElement> children(const QDomElement &e)
{
    QList<QDomElement> list;
    for (QDomElement c = e.firstChildElement(); !c.isNull(); c = c.nextSiblingElement())
        list << c;
    return list;
}

// A prefixed attribute ("r:id"), whatever the prefix. Slide list entries
// have both a numeric "id" and the relationship "r:id".
QString relationship(const QDomElement &e, const QString &name)
{
    const QDomNamedNodeMap attributes = e.attributes();
    for (int i = 0; i < attributes.count(); ++i) {
        const QDomAttr a = attributes.item(i).toAttr();
        if (a.name().endsWith(u':' + name))
            return a.value();
    }
    return {};
}

// An attribute by local name ("r:embed" matches "embed").
QString attribute(const QDomElement &e, const QString &name)
{
    if (e.hasAttribute(name))
        return e.attribute(name);
    return relationship(e, name);
}

double number(const QDomElement &e, const QString &name, double fallback)
{
    bool ok = false;
    const double value = attribute(e, name).toDouble(&ok);
    return ok ? value : fallback;
}

struct Part
{
    QString name;
    QDomDocument doc;
    QHash<QString, QString> targets; // relationship id -> part name
    QHash<QString, QString> types;   // relationship id -> type (last path segment)
    QDomElement root() const { return doc.documentElement(); }
    bool isNull() const { return doc.isNull(); }

    // The first related part of a type ("slideLayout", "slideMaster", "theme").
    QString related(const QString &type) const
    {
        for (auto it = types.cbegin(); it != types.cend(); ++it) {
            if (it.value() == type)
                return targets.value(it.key());
        }
        return {};
    }
};

QString resolve(const QString &fromPart, const QString &target)
{
    if (target.startsWith(u'/'))
        return target.mid(1);
    const QString folder = fromPart.left(fromPart.lastIndexOf(u'/') + 1);
    return QDir::cleanPath(folder + target);
}

QColor withAlpha(QColor colour, const QDomElement &spec)
{
    const QDomElement alpha = child(spec, u"alpha");
    if (!alpha.isNull())
        colour.setAlphaF(number(alpha, QStringLiteral("val"), 100000) / 100000.0);
    // Lighter and darker variants (theme tints and shades).
    const QDomElement lumMod = child(spec, u"lumMod");
    const QDomElement lumOff = child(spec, u"lumOff");
    if (!lumMod.isNull() || !lumOff.isNull()) {
        float h, s, l, a;
        colour.getHslF(&h, &s, &l, &a);
        l = float(l * number(lumMod, QStringLiteral("val"), 100000) / 100000.0
                  + number(lumOff, QStringLiteral("val"), 0) / 100000.0);
        colour = QColor::fromHslF(h, s, std::clamp(l, 0.0f, 1.0f), a);
    }
    return colour;
}

} // namespace

struct PptxReader::Impl
{
    QZipReader *zip = nullptr;
    // A map, not a QHash: references to parts must stay valid as more load.
    mutable std::map<QString, Part> parts;
    mutable QHash<QString, QImage> images;

    ~Impl() { delete zip; }

    const Part &part(const QString &name) const
    {
        if (auto it = parts.find(name); it != parts.end())
            return it->second;
        Part p;
        p.name = name;
        if (zip && !name.isEmpty()) {
            (void)p.doc.setContent(zip->fileData(name));
            const QString relsName = name.left(name.lastIndexOf(u'/') + 1) + QStringLiteral("_rels/")
                                     + name.mid(name.lastIndexOf(u'/') + 1) + QStringLiteral(".rels");
            QDomDocument rels;
            if (rels.setContent(zip->fileData(relsName))) {
                for (const QDomElement &r : children(rels.documentElement())) {
                    if (attribute(r, QStringLiteral("TargetMode")) == QLatin1String("External"))
                        continue;
                    const QString id = r.attribute(QStringLiteral("Id"));
                    p.targets.insert(id, resolve(name, r.attribute(QStringLiteral("Target"))));
                    p.types.insert(id, r.attribute(QStringLiteral("Type")).section(u'/', -1));
                }
            }
        }
        return parts.emplace(name, std::move(p)).first->second;
    }

    QImage image(const QString &name) const
    {
        auto it = images.find(name);
        if (it != images.end())
            return *it;
        QImage img;
        if (zip)
            img.loadFromData(zip->fileData(name));
        return *images.insert(name, img);
    }
};

// What one slide is drawn from: the slide, its layout and master, and the
// theme's colours and fonts.
struct Context
{
    const PptxReader::Impl *d = nullptr;
    const Part *slide = nullptr;
    const Part *layout = nullptr;
    const Part *master = nullptr;
    QHash<QString, QColor> colours; // dk1, lt1, accent1, ...
    QString majorFont = QStringLiteral("Calibri Light");
    QString minorFont = QStringLiteral("Calibri");
    double scale = 1.0; // pixels per EMU

    QColor colour(const QDomElement &fillOrColour, const QColor &fallback = {}) const
    {
        // Accepts a:solidFill or a colour element directly.
        QDomElement spec = fillOrColour;
        if (localName(spec) == QLatin1String("solidFill") || localName(spec) == QLatin1String("fontRef")
            || localName(spec) == QLatin1String("fillRef") || localName(spec) == QLatin1String("lnRef")
            || localName(spec) == QLatin1String("bgRef"))
            spec = spec.firstChildElement();
        if (spec.isNull())
            return fallback;
        const QString kind = localName(spec);
        const QString val = spec.attribute(QStringLiteral("val"));
        QColor c;
        if (kind == QLatin1String("srgbClr")) {
            c = QColor(u'#' + val);
        } else if (kind == QLatin1String("schemeClr")) {
            static const QHash<QString, QString> aliases{{"bg1", "lt1"}, {"tx1", "dk1"}, {"bg2", "lt2"}, {"tx2", "dk2"}};
            c = colours.value(aliases.value(val, val), fallback);
        } else if (kind == QLatin1String("sysClr")) {
            c = QColor(u'#' + spec.attribute(QStringLiteral("lastClr"), QStringLiteral("000000")));
        } else if (kind == QLatin1String("prstClr")) {
            c = QColor(val);
        }
        if (!c.isValid())
            return fallback;
        return withAlpha(c, spec);
    }

    QString font(const QString &typeface, bool title) const
    {
        if (typeface.isEmpty() || typeface == QLatin1String("+mj-lt"))
            return typeface.isEmpty() ? (title ? majorFont : minorFont) : majorFont;
        if (typeface == QLatin1String("+mn-lt"))
            return minorFont;
        return typeface;
    }
};

namespace {

// The placeholder kind for text styles: "title" or "body" (or "other" for
// ordinary shapes).
QString placeholderType(const QDomElement &shape)
{
    const QDomElement ph = path(shape, {"nvSpPr", "nvPr", "ph"});
    if (ph.isNull())
        return {};
    const QString type = ph.attribute(QStringLiteral("type"));
    return type.isEmpty() ? QStringLiteral("body") : type;
}

bool isTitle(const QString &type)
{
    return type == QLatin1String("title") || type == QLatin1String("ctrTitle");
}

// The shape in a layout or master that a slide placeholder takes its place
// and styles from: same index, else same type.
QDomElement matchingPlaceholder(const Part *part, const QDomElement &shape)
{
    if (!part || part->isNull())
        return {};
    const QDomElement ph = path(shape, {"nvSpPr", "nvPr", "ph"});
    if (ph.isNull())
        return {};
    const QString idx = ph.attribute(QStringLiteral("idx"));
    QString type = placeholderType(shape);
    QDomElement byType;
    const QDomElement tree = path(part->root(), {"cSld", "spTree"});
    for (const QDomElement &candidate : children(tree)) {
        const QDomElement cph = path(candidate, {"nvSpPr", "nvPr", "ph"});
        if (cph.isNull())
            continue;
        if (!idx.isEmpty() && cph.attribute(QStringLiteral("idx")) == idx)
            return candidate;
        const QString ctype = placeholderType(candidate);
        if (byType.isNull() && (ctype == type || (isTitle(type) && isTitle(ctype))
                                || (type == QLatin1String("subTitle") && ctype == QLatin1String("body"))
                                || (type == QLatin1String("obj") && ctype == QLatin1String("body"))))
            byType = candidate;
    }
    return byType;
}

QRectF frameOf(const QDomElement &spPr)
{
    const QDomElement xfrm = child(spPr, u"xfrm");
    if (xfrm.isNull())
        return {};
    const QDomElement off = child(xfrm, u"off");
    const QDomElement ext = child(xfrm, u"ext");
    if (off.isNull() || ext.isNull())
        return {};
    return QRectF(number(off, QStringLiteral("x"), 0), number(off, QStringLiteral("y"), 0),
                  number(ext, QStringLiteral("cx"), 0), number(ext, QStringLiteral("cy"), 0));
}

class SlidePainter
{
public:
    SlidePainter(QPainter &painter, const Context &ctx) : p(painter), c(ctx) {}

    void background()
    {
        // The slide's own background, else its layout's, else its master's.
        for (const Part *part : {c.slide, c.layout, c.master}) {
            if (!part || part->isNull())
                continue;
            const QDomElement bg = path(part->root(), {"cSld", "bg"});
            if (bg.isNull())
                continue;
            const QDomElement bgPr = child(bg, u"bgPr");
            if (!bgPr.isNull()) {
                fillArea(bgPr, *part, QRectF(QPointF(0, 0), deviceRect().size()));
                return;
            }
            const QDomElement bgRef = child(bg, u"bgRef");
            if (!bgRef.isNull()) {
                p.fillRect(deviceRect(), c.colour(bgRef, Qt::white));
                return;
            }
        }
        p.fillRect(deviceRect(), c.colours.value(QStringLiteral("lt1"), Qt::white));
    }

    // Shapes from the master and layout that are not placeholders (logos,
    // bands), unless the slide or layout turns them off, then the slide's.
    void shapes()
    {
        const auto showsMaster = [](const Part *part) {
            return !part || part->root().attribute(QStringLiteral("showMasterSp")) != QLatin1String("0");
        };
        if (showsMaster(c.slide) && showsMaster(c.layout) && c.master)
            tree(path(c.master->root(), {"cSld", "spTree"}), *c.master, QTransform(), true);
        if (showsMaster(c.slide) && c.layout)
            tree(path(c.layout->root(), {"cSld", "spTree"}), *c.layout, QTransform(), true);
        tree(path(c.slide->root(), {"cSld", "spTree"}), *c.slide, QTransform(), false);
    }

private:
    QPainter &p;
    const Context &c;

    QRect deviceRect() const { return QRect(0, 0, p.device()->width(), p.device()->height()); }

    QRectF toPixels(const QRectF &emu, const QTransform &group) const
    {
        const QRectF r = group.mapRect(emu);
        return QRectF(r.x() * c.scale, r.y() * c.scale, r.width() * c.scale, r.height() * c.scale);
    }

    // Fills an area from a:solidFill, a:gradFill or a:blipFill under `props`.
    // Returns false if `props` has no fill.
    bool fillArea(const QDomElement &props, const Part &part, const QRectF &area, const QPainterPath *shape = nullptr)
    {
        const auto fill = [&](const QBrush &brush) {
            if (shape)
                p.fillPath(*shape, brush);
            else
                p.fillRect(area, brush);
        };
        if (const QDomElement solid = child(props, u"solidFill"); !solid.isNull()) {
            fill(c.colour(solid, Qt::white));
            return true;
        }
        if (const QDomElement grad = child(props, u"gradFill"); !grad.isNull()) {
            QLinearGradient gradient(area.topLeft(), area.bottomLeft());
            for (const QDomElement &stop : children(child(grad, u"gsLst")))
                gradient.setColorAt(number(stop, QStringLiteral("pos"), 0) / 100000.0, c.colour(stop.firstChildElement(), Qt::white));
            fill(gradient);
            return true;
        }
        if (const QDomElement blip = path(props, {"blipFill", "blip"}); !blip.isNull()) {
            const QImage img = c.d->image(part.targets.value(attribute(blip, QStringLiteral("embed"))));
            if (!img.isNull()) {
                p.save();
                if (shape)
                    p.setClipPath(*shape);
                p.drawImage(area, img);
                p.restore();
            }
            return true;
        }
        return !child(props, u"noFill").isNull();
    }

    void tree(const QDomElement &spTree, const Part &part, const QTransform &group, bool skipPlaceholders)
    {
        for (const QDomElement &e : children(spTree)) {
            const QString kind = localName(e);
            if (kind == QLatin1String("sp")) {
                if (skipPlaceholders && !path(e, {"nvSpPr", "nvPr", "ph"}).isNull())
                    continue;
                shape(e, part, group);
            } else if (kind == QLatin1String("pic")) {
                picture(e, part, group);
            } else if (kind == QLatin1String("grpSp")) {
                const QDomElement xfrm = path(e, {"grpSpPr", "xfrm"});
                const QDomElement off = child(xfrm, u"off"), ext = child(xfrm, u"ext");
                const QDomElement chOff = child(xfrm, u"chOff"), chExt = child(xfrm, u"chExt");
                QTransform t;
                if (!off.isNull() && !chExt.isNull()) {
                    const double sx = number(ext, QStringLiteral("cx"), 1) / std::max(1.0, number(chExt, QStringLiteral("cx"), 1));
                    const double sy = number(ext, QStringLiteral("cy"), 1) / std::max(1.0, number(chExt, QStringLiteral("cy"), 1));
                    t.translate(number(off, QStringLiteral("x"), 0), number(off, QStringLiteral("y"), 0));
                    t.scale(sx, sy);
                    t.translate(-number(chOff, QStringLiteral("x"), 0), -number(chOff, QStringLiteral("y"), 0));
                }
                tree(e, part, t * group, skipPlaceholders);
            }
            // Tables, charts and other graphic frames are not drawn.
        }
    }

    void withRotation(const QDomElement &spPr, const QRectF &rect, const std::function<void()> &draw)
    {
        const double rotation = number(child(spPr, u"xfrm"), QStringLiteral("rot"), 0) / 60000.0;
        p.save();
        if (rotation != 0) {
            p.translate(rect.center());
            p.rotate(rotation);
            p.translate(-rect.center());
        }
        draw();
        p.restore();
    }

    void picture(const QDomElement &pic, const Part &part, const QTransform &group)
    {
        const QDomElement spPr = child(pic, u"spPr");
        const QRectF rect = toPixels(frameOf(spPr), group);
        const QDomElement blip = path(pic, {"blipFill", "blip"});
        const QImage img = c.d->image(part.targets.value(attribute(blip, QStringLiteral("embed"))));
        if (rect.isEmpty() || img.isNull())
            return;
        // Cropping: l, t, r, b in thousandths of a percent.
        QRectF source(img.rect());
        if (const QDomElement crop = path(pic, {"blipFill", "srcRect"}); !crop.isNull()) {
            const double l = number(crop, QStringLiteral("l"), 0) / 100000.0, t = number(crop, QStringLiteral("t"), 0) / 100000.0;
            const double r = number(crop, QStringLiteral("r"), 0) / 100000.0, b = number(crop, QStringLiteral("b"), 0) / 100000.0;
            source = QRectF(img.width() * l, img.height() * t, img.width() * (1 - l - r), img.height() * (1 - t - b));
        }
        withRotation(spPr, rect, [&] { p.drawImage(rect, img, source); });
    }

    void shape(const QDomElement &sp, const Part &part, const QTransform &group)
    {
        const QDomElement spPr = child(sp, u"spPr");
        QRectF emu = frameOf(spPr);
        // A placeholder without its own position takes its layout's, then
        // its master's.
        const QDomElement fromLayout = (&part == c.slide) ? matchingPlaceholder(c.layout, sp) : QDomElement();
        const QDomElement fromMaster = matchingPlaceholder(c.master, fromLayout.isNull() ? sp : fromLayout);
        if (emu.isEmpty())
            emu = frameOf(child(fromLayout, u"spPr"));
        if (emu.isEmpty())
            emu = frameOf(child(fromMaster, u"spPr"));
        const QRectF rect = toPixels(emu, group);
        if (rect.isEmpty())
            return;

        withRotation(spPr, rect, [&] {
            QPainterPath outline;
            const QString geometry = path(spPr, {"prstGeom"}).attribute(QStringLiteral("prst"), QStringLiteral("rect"));
            if (geometry == QLatin1String("ellipse"))
                outline.addEllipse(rect);
            else if (geometry == QLatin1String("roundRect"))
                outline.addRoundedRect(rect, std::min(rect.width(), rect.height()) * 0.1667,
                                       std::min(rect.width(), rect.height()) * 0.1667);
            else
                outline.addRect(rect);

            // Fill: the shape's own, else its style's (shapes drawn in
            // PowerPoint carry a style reference to a theme colour).
            if (!fillArea(spPr, part, rect, &outline)) {
                const QDomElement fillRef = path(sp, {"style", "fillRef"});
                if (!fillRef.isNull() && fillRef.attribute(QStringLiteral("idx")) != QLatin1String("0"))
                    p.fillPath(outline, c.colour(fillRef, Qt::transparent));
            }
            const QDomElement ln = child(spPr, u"ln");
            if (const QDomElement lineFill = child(ln, u"solidFill"); !lineFill.isNull()) {
                QPen pen(c.colour(lineFill, Qt::black));
                pen.setWidthF(std::max(1.0, number(ln, QStringLiteral("w"), 12700) * c.scale));
                p.strokePath(outline, pen);
            }
            text(sp, fromLayout, fromMaster, rect);
        });
    }

    // Text styles apply in order: the paragraph, the shape's list style, the
    // layout placeholder's, the master placeholder's, then the master's text
    // styles for titles, body text or other text.
    void text(const QDomElement &sp, const QDomElement &fromLayout, const QDomElement &fromMaster, const QRectF &rect)
    {
        const QDomElement body = child(sp, u"txBody");
        if (body.isNull())
            return;
        const QString type = placeholderType(sp);
        const bool title = isTitle(type);
        const QDomElement txStyles = child(c.master ? c.master->root() : QDomElement(), u"txStyles");
        const QDomElement masterStyle = child(txStyles, title ? u"titleStyle" : type.isEmpty() ? u"otherStyle" : u"bodyStyle");
        const QList<QDomElement> styleLists{child(body, u"lstStyle"), path(fromLayout, {"txBody", "lstStyle"}),
                                            path(fromMaster, {"txBody", "lstStyle"}), masterStyle};
        const auto levelProps = [&](int level, const char *name) -> QDomElement {
            for (const QDomElement &list : styleLists) {
                const QDomElement lvl = child(list, QStringLiteral("lvl%1pPr").arg(level + 1));
                const QDomElement found = name ? child(lvl, QLatin1String(name)) : lvl;
                if (!found.isNull())
                    return found;
            }
            return {};
        };
        const auto levelAttribute = [&](int level, const QString &name) -> QString {
            for (const QDomElement &list : styleLists) {
                const QDomElement lvl = child(list, QStringLiteral("lvl%1pPr").arg(level + 1));
                if (lvl.hasAttribute(name))
                    return lvl.attribute(name);
            }
            return {};
        };
        // Body properties (insets, anchor, autofit) from the shape, else
        // the layout's or master's placeholder.
        const QDomElement bodyPr = child(body, u"bodyPr");
        const auto bodyAttribute = [&](const QString &name, const QString &fallback) {
            for (const QDomElement &b : {bodyPr, path(fromLayout, {"txBody", "bodyPr"}), path(fromMaster, {"txBody", "bodyPr"})}) {
                if (b.hasAttribute(name))
                    return b.attribute(name);
            }
            return fallback;
        };
        const QDomElement styleFont = path(sp, {"style", "fontRef"});
        const QColor shapeTextColour = c.colour(styleFont, c.colours.value(QStringLiteral("dk1"), Qt::black));

        const double left = bodyAttribute(QStringLiteral("lIns"), QStringLiteral("91440")).toDouble() * c.scale;
        const double right = bodyAttribute(QStringLiteral("rIns"), QStringLiteral("91440")).toDouble() * c.scale;
        const double top = bodyAttribute(QStringLiteral("tIns"), QStringLiteral("45720")).toDouble() * c.scale;
        const double bottom = bodyAttribute(QStringLiteral("bIns"), QStringLiteral("45720")).toDouble() * c.scale;
        const QRectF box = rect.adjusted(left, top, -right, -bottom);
        if (box.width() <= 1 || box.height() <= 1)
            return;
        const QString anchor = bodyAttribute(QStringLiteral("anchor"), QStringLiteral("t"));
        const QDomElement autofit = child(bodyPr, u"normAutofit");
        double fontScale = autofit.isNull() ? 1.0 : number(autofit, QStringLiteral("fontScale"), 100000) / 100000.0;
        const double pixelsPerPoint = emuPerPoint * c.scale;

        const auto build = [&](QTextDocument &doc, double scale) {
            doc.clear();
            doc.setDocumentMargin(0);
            doc.setTextWidth(bodyAttribute(QStringLiteral("wrap"), QStringLiteral("square")) == QLatin1String("none") ? -1 : box.width());
            QTextCursor cursor(&doc);
            bool first = true;
            for (const QDomElement &paragraph : children(body)) {
                if (localName(paragraph) != QLatin1String("p"))
                    continue;
                const QDomElement pPr = child(paragraph, u"pPr");
                const int level = int(number(pPr, QStringLiteral("lvl"), 0));
                QTextBlockFormat block;
                const QString align = pPr.hasAttribute(QStringLiteral("algn")) ? pPr.attribute(QStringLiteral("algn"))
                                                                                : levelAttribute(level, QStringLiteral("algn"));
                block.setAlignment(align == QLatin1String("ctr") ? Qt::AlignHCenter
                                   : align == QLatin1String("r") ? Qt::AlignRight
                                   : align == QLatin1String("just") ? Qt::AlignJustify : Qt::AlignLeft);
                const QString marL = pPr.hasAttribute(QStringLiteral("marL")) ? pPr.attribute(QStringLiteral("marL"))
                                                                               : levelAttribute(level, QStringLiteral("marL"));
                block.setLeftMargin(marL.toDouble() * c.scale);
                if (first)
                    cursor.setBlockFormat(block);
                else
                    cursor.insertBlock(block);
                first = false;

                // The paragraph's default run properties (end of paragraph).
                const QDomElement levelRun = levelProps(level, "defRPr");
                const auto format = [&](const QDomElement &rPr) {
                    QTextCharFormat f;
                    const double size = rPr.hasAttribute(QStringLiteral("sz")) ? number(rPr, QStringLiteral("sz"), 1800)
                                                                              : number(levelRun, QStringLiteral("sz"), 1800);
                    f.setFont(QFont(c.font(attribute(child(rPr, u"latin").isNull() ? child(levelRun, u"latin") : child(rPr, u"latin"),
                                                     QStringLiteral("typeface")),
                                           title)));
                    QFont font = f.font();
                    font.setPixelSize(std::max(1, int(std::lround(size / 100.0 * pixelsPerPoint * scale))));
                    const QString b = rPr.hasAttribute(QStringLiteral("b")) ? rPr.attribute(QStringLiteral("b")) : levelRun.attribute(QStringLiteral("b"));
                    const QString i = rPr.hasAttribute(QStringLiteral("i")) ? rPr.attribute(QStringLiteral("i")) : levelRun.attribute(QStringLiteral("i"));
                    font.setBold(b == QLatin1String("1") || b == QLatin1String("true"));
                    font.setItalic(i == QLatin1String("1") || i == QLatin1String("true"));
                    f.setFont(font);
                    QDomElement fill = child(rPr, u"solidFill");
                    if (fill.isNull())
                        fill = child(levelRun, u"solidFill");
                    f.setForeground(fill.isNull() ? shapeTextColour : c.colour(fill, shapeTextColour));
                    return f;
                };

                // Bullet: the paragraph's own, else its level's.
                bool hasText = false;
                for (const QDomElement &run : children(paragraph))
                    hasText = hasText || !child(run, u"t").text().isEmpty();
                QString bullet;
                if (hasText && child(pPr, u"buNone").isNull()) {
                    // The paragraph's own bullet wins; else its level's,
                    // unless the level turns bullets off.
                    QDomElement buChar = child(pPr, u"buChar");
                    if (buChar.isNull() && levelProps(level, "buNone").isNull())
                        buChar = levelProps(level, "buChar");
                    if (!buChar.isNull())
                        bullet = buChar.attribute(QStringLiteral("char"), QStringLiteral("•"));
                }
                if (!bullet.isEmpty()) {
                    QDomElement firstRun = child(child(paragraph, u"r"), u"rPr");
                    cursor.insertText(bullet + QStringLiteral(" "), format(firstRun));
                }
                for (const QDomElement &run : children(paragraph)) {
                    const QString kind = localName(run);
                    if (kind == QLatin1String("r") || kind == QLatin1String("fld"))
                        cursor.insertText(child(run, u"t").text(), format(child(run, u"rPr")));
                    else if (kind == QLatin1String("br"))
                        cursor.insertText(QString(QChar::LineSeparator), format(child(run, u"rPr")));
                }
                if (!hasText)
                    cursor.setCharFormat(format(child(paragraph, u"endParaRPr")));
            }
        };

        QTextDocument doc;
        build(doc, fontScale);
        // "Shrink text on overflow": shrink until it fits, as PowerPoint does.
        while (!autofit.isNull() && doc.size().height() > box.height() && fontScale > 0.3) {
            fontScale *= 0.9;
            build(doc, fontScale);
        }
        double y = box.top();
        if (anchor == QLatin1String("ctr"))
            y = box.top() + (box.height() - doc.size().height()) / 2;
        else if (anchor == QLatin1String("b"))
            y = box.bottom() - doc.size().height();
        p.save();
        p.translate(box.left(), y);
        doc.drawContents(&p);
        p.restore();
    }
};

} // namespace

PptxReader::PptxReader() : d(std::make_unique<Impl>()) {}
PptxReader::~PptxReader() = default;

int PptxReader::visibleCount() const
{
    return int(std::count_if(m_slides.begin(), m_slides.end(), [](const SlideInfo &s) { return !s.hidden; }));
}

bool PptxReader::open(const QString &filePath)
{
    m_slides.clear();
    d = std::make_unique<Impl>();
    d->zip = new QZipReader(filePath);
    if (!d->zip->isReadable() || d->zip->status() != QZipReader::NoError) {
        m_error = QStringLiteral("not a PowerPoint file (it could not be opened as a .pptx)");
        return false;
    }
    const Part &presentation = d->part(QStringLiteral("ppt/presentation.xml"));
    if (presentation.isNull()) {
        m_error = QStringLiteral("not a PowerPoint file (no slides found)");
        return false;
    }
    const QDomElement size = child(presentation.root(), u"sldSz");
    if (!size.isNull())
        m_size = QSizeF(number(size, QStringLiteral("cx"), 9144000) / emuPerPoint, number(size, QStringLiteral("cy"), 5143500) / emuPerPoint);
    // The show order is the order of the slide list, whatever the files are
    // called inside the zip.
    for (const QDomElement &id : children(child(presentation.root(), u"sldIdLst"))) {
        const QString target = presentation.targets.value(relationship(id, QStringLiteral("id")));
        const Part &slide = d->part(target);
        if (target.isEmpty() || slide.isNull())
            continue;
        m_slides.push_back({target, slide.root().attribute(QStringLiteral("show")) == QLatin1String("0")});
    }
    if (m_slides.empty()) {
        m_error = QStringLiteral("the presentation has no slides");
        return false;
    }
    return true;
}

QImage PptxReader::render(int index, int width) const
{
    if (index < 0 || index >= int(m_slides.size()) || m_size.isEmpty())
        return {};
    Context ctx;
    ctx.d = d.get();
    ctx.slide = &d->part(m_slides[size_t(index)].part);
    const QString layoutName = ctx.slide->related(QStringLiteral("slideLayout"));
    ctx.layout = layoutName.isEmpty() ? nullptr : &d->part(layoutName);
    const QString masterName = ctx.layout ? ctx.layout->related(QStringLiteral("slideMaster")) : QString();
    ctx.master = masterName.isEmpty() ? nullptr : &d->part(masterName);
    if (ctx.master) {
        const Part &theme = d->part(ctx.master->related(QStringLiteral("theme")));
        const QDomElement elements = child(theme.root(), u"themeElements");
        for (const QDomElement &entry : children(child(elements, u"clrScheme")))
            ctx.colours.insert(localName(entry), ctx.colour(entry.firstChildElement()));
        const QDomElement fonts = child(elements, u"fontScheme");
        if (const QString major = path(fonts, {"majorFont", "latin"}).attribute(QStringLiteral("typeface")); !major.isEmpty())
            ctx.majorFont = major;
        if (const QString minor = path(fonts, {"minorFont", "latin"}).attribute(QStringLiteral("typeface")); !minor.isEmpty())
            ctx.minorFont = minor;
    }
    if (!ctx.colours.contains(QStringLiteral("dk1")))
        ctx.colours.insert(QStringLiteral("dk1"), Qt::black);
    if (!ctx.colours.contains(QStringLiteral("lt1")))
        ctx.colours.insert(QStringLiteral("lt1"), Qt::white);

    const int height = std::max(1, int(std::lround(width * m_size.height() / m_size.width())));
    ctx.scale = double(width) / (m_size.width() * emuPerPoint);
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing | QPainter::SmoothPixmapTransform);
    SlidePainter slide(painter, ctx);
    slide.background();
    slide.shapes();
    painter.end();
    return image;
}

} // namespace lumen::app
