#include "lumen/core/songs/songimport.h"

#include "lumen/core/textfold.h"

#include <QFileInfo>
#include <QHash>
#include <QRegularExpression>
#include <QStringDecoder>
#include <QXmlStreamReader>

#include <algorithm>
#include <array>
#include <optional>

namespace lumen::core::songs {

// ---- small helpers ------------------------------------------------------

int ParsedSection::lineCount() const
{
    int count = 0;
    for (const auto &block : blocks)
        count += int(block.size());
    return count;
}

QStringList ParsedSection::texts() const
{
    QStringList out;
    for (const auto &block : blocks) {
        for (const LyricLine &line : block)
            out << line.text;
    }
    return out;
}

namespace {

Error invalid(const QString &message)
{
    return Error{Error::Code::Invalid, message};
}

// Windows-1252 bytes 0x80-0x9F; the rest of the range matches Latin-1.
constexpr std::array<char16_t, 32> cp1252High{
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};

QString decodeWindows1252(const QByteArray &bytes)
{
    QString text;
    text.reserve(bytes.size());
    for (const char byte : bytes) {
        const auto b = static_cast<unsigned char>(byte);
        text += (b >= 0x80 && b <= 0x9F) ? QChar(cp1252High[b - 0x80]) : QChar(b);
    }
    return text;
}

// Turns a section name from OpenLyrics ("v1", "c", "c2", "p", "v1a") into
// Lumen's label.
QString labelFromOpenLyricsName(const QString &name)
{
    static const QRegularExpression pattern(QStringLiteral(R"(^([a-z]+)(\d*)([a-z]?)$)"),
                                            QRegularExpression::CaseInsensitiveOption);
    const auto match = pattern.match(name.trimmed());
    if (!match.hasMatch())
        return name.trimmed();
    const QString kind = match.captured(1).toLower();
    const QString number = match.captured(2);
    const QString part = match.captured(3).toLower();

    QString base;
    if (kind == QLatin1String("v"))
        return QStringLiteral("Verse ") + (number.isEmpty() ? QStringLiteral("1") : number) + part;
    if (kind == QLatin1String("c"))
        base = QStringLiteral("Chorus");
    else if (kind == QLatin1String("p") || kind == QLatin1String("pc") || kind == QLatin1String("pre"))
        base = QStringLiteral("Pre-chorus");
    else if (kind == QLatin1String("b"))
        base = QStringLiteral("Bridge");
    else if (kind == QLatin1String("e") || kind == QLatin1String("o"))
        base = QStringLiteral("Ending");
    else if (kind == QLatin1String("i"))
        base = QStringLiteral("Intro");
    else if (kind == QLatin1String("t"))
        base = QStringLiteral("Tag");
    else
        return name.trimmed();
    if (!number.isEmpty() && number != QLatin1String("1"))
        base += u' ' + number;
    return base + part;
}

} // namespace

// ---- encodings and names --------------------------------------------------

QString decodeText(const QByteArray &bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF"))
        return QString::fromUtf8(bytes.mid(3));
    if (bytes.startsWith("\xFF\xFE")) {
        QStringDecoder utf16(QStringDecoder::Utf16LE);
        return utf16(bytes.mid(2));
    }
    if (bytes.startsWith("\xFE\xFF")) {
        QStringDecoder utf16(QStringDecoder::Utf16BE);
        return utf16(bytes.mid(2));
    }
    QStringDecoder utf8(QStringDecoder::Utf8, QStringDecoder::Flag::Stateless);
    const QString text = utf8(bytes);
    if (!utf8.hasError())
        return text;
    return decodeWindows1252(bytes);
}

QString titleFromFileName(const QString &fileName)
{
    QString base = QFileInfo(fileName).completeBaseName();
    // Drop a leading number such as "017 " or "3_" that orders files.
    static const QRegularExpression number(QStringLiteral(R"(^\d+[\s._-]+(?=\S))"));
    base.remove(number);
    base.replace(u'_', u' ').replace(u'-', u' ');
    QStringList words = base.simplified().split(u' ', Qt::SkipEmptyParts);
    for (QString &word : words)
        word[0] = word[0].toUpper();
    return words.join(u' ');
}

QString sectionLabelFromHeader(const QString &line)
{
    static const QRegularExpression header(
        QStringLiteral(R"(^\s*[\[\(]?\s*(verse|v|chorus|c|refrain|pre-?\s*chorus|pc|bridge|b|tag|)"
                       R"(ending|end|outro|coda|intro|interlude)\s*(\d+)?\s*[\]\)]?\s*:?\s*$)"),
        QRegularExpression::CaseInsensitiveOption);
    const auto match = header.match(line);
    if (!match.hasMatch())
        return {};
    const QString kind = match.captured(1).toLower().remove(u' ').remove(u'-');
    const QString number = match.captured(2);
    const bool extra = !number.isEmpty() && number != QLatin1String("1");

    if (kind == QLatin1String("verse") || kind == QLatin1String("v"))
        return number.isEmpty() ? QStringLiteral("Verse") : QStringLiteral("Verse ") + number;
    QString base;
    if (kind == QLatin1String("chorus") || kind == QLatin1String("c") || kind == QLatin1String("refrain"))
        base = QStringLiteral("Chorus");
    else if (kind == QLatin1String("prechorus") || kind == QLatin1String("pc"))
        base = QStringLiteral("Pre-chorus");
    else if (kind == QLatin1String("bridge") || kind == QLatin1String("b"))
        base = QStringLiteral("Bridge");
    else if (kind == QLatin1String("tag"))
        base = QStringLiteral("Tag");
    else if (kind == QLatin1String("intro"))
        base = QStringLiteral("Intro");
    else if (kind == QLatin1String("interlude"))
        base = QStringLiteral("Interlude");
    else
        base = QStringLiteral("Ending");
    return extra ? base + u' ' + number : base;
}

// ---- plain text ------------------------------------------------------------
//
// Accepted layouts, freely mixed:
//   Title: / Author: / Copyright: / CCLI: / Key: / Tempo: lines at the top
//   A lone first line followed by labelled sections is the title (CCLI style)
//   Section headers on their own line: "Verse 1", "[Chorus]", "V2:", "Bridge"
//   Stanzas separated by blank lines; unlabelled ones become verses
//   A header with no lines repeats that section ("Chorus" again)
//   An unlabelled stanza written out again is a chorus
//   "---" inside a section forces a slide break
//   Trailing "© ..." and "CCLI Song # ..." lines are read as details

namespace {

struct Stanza
{
    QString label; // canonical, or empty
    std::vector<std::vector<LyricLine>> blocks{{}};

    bool hasLines() const
    {
        for (const auto &block : blocks) {
            if (!block.empty())
                return true;
        }
        return false;
    }
};

ParsedSection sectionFrom(const QString &label, const Stanza &stanza)
{
    ParsedSection section{label, {}};
    for (const auto &block : stanza.blocks) {
        if (!block.empty())
            section.blocks.push_back(block);
    }
    return section;
}

} // namespace

Result<ParsedSong> parseTextSong(const QString &text, const QString &fallbackTitle)
{
    QString normalised = text;
    normalised.replace(QStringLiteral("\r\n"), QStringLiteral("\n")).replace(u'\r', u'\n');
    QStringList lines = normalised.split(u'\n');

    ParsedSong song;

    // Details at the top.
    static const QRegularExpression meta(
        QStringLiteral(R"(^\s*(title|authors?|words|copyright|ccli(?:\s*(?:song\s*)?(?:number|no\.?|#))?|key|tempo)\s*[:#]\s*(.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype start = 0;
    for (; start < lines.size(); ++start) {
        const QString &line = lines[start];
        if (line.trimmed().isEmpty())
            continue;
        const auto match = meta.match(line);
        if (!match.hasMatch())
            break;
        const QString field = match.captured(1).toLower();
        const QString value = match.captured(2).trimmed();
        if (field == QLatin1String("title"))
            song.title = value;
        else if (field.startsWith(QLatin1String("author")) || field == QLatin1String("words"))
            song.authors << value;
        else if (field == QLatin1String("copyright"))
            song.copyright = value;
        else if (field.startsWith(QLatin1String("ccli")))
            song.ccli = value;
        else if (field == QLatin1String("key"))
            song.key = value;
        else if (field == QLatin1String("tempo"))
            song.tempo = value;
    }

    // Details at the bottom (CCLI SongSelect style).
    static const QRegularExpression footer(
        QStringLiteral(R"(^\s*(?:(©|\(c\)|copyright\b)\s*(.*)|ccli\s*song\s*(?:#|no\.?|number)?\s*:?\s*(\d+).*|ccli\s*licen[cs]e.*|for use solely.*)$)"),
        QRegularExpression::CaseInsensitiveOption);
    qsizetype end = lines.size();
    while (end > start) {
        const QString &line = lines[end - 1];
        if (line.trimmed().isEmpty()) {
            --end;
            continue;
        }
        const auto match = footer.match(line);
        if (!match.hasMatch())
            break;
        if (!match.captured(1).isEmpty() && song.copyright.isEmpty())
            song.copyright = match.captured(2).trimmed();
        if (!match.captured(3).isEmpty() && song.ccli.isEmpty())
            song.ccli = match.captured(3);
        --end;
    }

    // Stanzas.
    std::vector<Stanza> stanzas;
    std::optional<Stanza> current;
    auto close = [&] {
        if (current && (current->hasLines() || !current->label.isEmpty()))
            stanzas.push_back(*current);
        current.reset();
    };
    for (qsizetype i = start; i < end; ++i) {
        const QString line = lines[i].trimmed();
        if (line.isEmpty()) {
            close();
            continue;
        }
        if (line == QLatin1String("---")) {
            if (current && !current->blocks.back().empty())
                current->blocks.push_back({});
            continue;
        }
        if (const QString label = sectionLabelFromHeader(line); !label.isEmpty()) {
            close();
            current = Stanza{label};
            continue;
        }
        if (!current)
            current = Stanza{};
        current->blocks.back().push_back({line, {}});
    }
    close();

    // A lone first line before sections that are all labelled is the title
    // (the CCLI SongSelect layout). Otherwise it is lyrics.
    const bool restLabelled =
        stanzas.size() > 1
        && std::all_of(stanzas.begin() + 1, stanzas.end(),
                       [](const Stanza &s) { return !s.label.isEmpty() || !s.hasLines(); });
    if (!stanzas.empty() && stanzas.front().label.isEmpty() && restLabelled
        && stanzas.front().blocks.size() == 1 && stanzas.front().blocks.front().size() == 1) {
        if (song.title.isEmpty())
            song.title = stanzas.front().blocks.front().front().text;
        stanzas.erase(stanzas.begin());
    }

    // A header on its own, then an unlabelled stanza: the header names it
    // (unless the header repeats a section seen before).
    QStringList seen;
    for (std::size_t i = 0; i < stanzas.size(); ++i) {
        Stanza &s = stanzas[i];
        if (!s.label.isEmpty() && !s.hasLines() && !seen.contains(s.label) && i + 1 < stanzas.size()
            && stanzas[i + 1].label.isEmpty() && stanzas[i + 1].hasLines()) {
            stanzas[i + 1].label = s.label;
            s.label.clear(); // now an empty stanza: dropped below
            continue;
        }
        if (!s.label.isEmpty() && s.hasLines())
            seen << s.label;
    }

    // Sections and play order. Unlabelled stanzas get placeholder keys and are
    // named once we know which ones repeat.
    const QString autoPrefix = QStringLiteral("\x01auto");
    QHash<QString, bool> mayBeChorus; // placeholder -> unlabelled (not "Verse")
    auto findByLyrics = [&song](const Stanza &stanza) -> QString {
        const ParsedSection candidate = sectionFrom({}, stanza);
        for (const ParsedSection &section : song.sections) {
            if (section.texts() == candidate.texts())
                return section.label;
        }
        return {};
    };
    auto exists = [&song](const QString &label) {
        return std::any_of(song.sections.begin(), song.sections.end(),
                           [&](const ParsedSection &s) { return s.label == label; });
    };
    int autoCount = 0;

    for (const Stanza &stanza : stanzas) {
        if (stanza.label.isEmpty() || stanza.label == QLatin1String("Verse")) {
            if (!stanza.hasLines())
                continue;
            if (const QString same = findByLyrics(stanza); !same.isEmpty()) {
                song.order << same;
                continue;
            }
            const QString key = autoPrefix + QString::number(autoCount++);
            mayBeChorus.insert(key, stanza.label.isEmpty());
            song.sections.push_back(sectionFrom(key, stanza));
            song.order << key;
            continue;
        }
        if (!stanza.hasLines()) {
            if (exists(stanza.label))
                song.order << stanza.label;
            else
                song.warnings << QStringLiteral("\"%1\" has no lines and was skipped").arg(stanza.label);
            continue;
        }
        if (exists(stanza.label)) {
            if (findByLyrics(stanza) == stanza.label) {
                song.order << stanza.label;
                continue;
            }
            int n = 2;
            while (exists(stanza.label + u' ' + QString::number(n)))
                ++n;
            const QString label = stanza.label + u' ' + QString::number(n);
            song.sections.push_back(sectionFrom(label, stanza));
            song.order << label;
            continue;
        }
        song.sections.push_back(sectionFrom(stanza.label, stanza));
        song.order << stanza.label;
    }

    // Name the placeholders: unlabelled stanzas that repeat are choruses, the
    // rest are verses numbered around any verses the file labelled itself.
    QHash<QString, QString> names;
    int choruses = 0;
    for (const ParsedSection &section : song.sections) {
        if (section.label.startsWith(autoPrefix) && mayBeChorus.value(section.label)
            && song.order.count(section.label) > 1) {
            QString name = QStringLiteral("Chorus");
            if (choruses > 0 || exists(name))
                name += u' ' + QString::number(choruses + (exists(QStringLiteral("Chorus")) ? 2 : 1));
            ++choruses;
            names.insert(section.label, name);
        }
    }
    int verse = 1;
    for (const ParsedSection &section : song.sections) {
        if (!section.label.startsWith(autoPrefix) || names.contains(section.label))
            continue;
        while (exists(QStringLiteral("Verse %1").arg(verse)))
            ++verse;
        names.insert(section.label, QStringLiteral("Verse %1").arg(verse++));
    }
    for (ParsedSection &section : song.sections)
        section.label = names.value(section.label, section.label);
    for (QString &label : song.order)
        label = names.value(label, label);

    if (song.sections.empty())
        return invalid(QStringLiteral("No lyrics found"));
    if (song.title.isEmpty())
        song.title = fallbackTitle;
    return song;
}

// ---- OpenLyrics -------------------------------------------------------------

namespace {

// Reads one <lines> element into lines, splitting at <br/> and collecting
// chord names. Formatting whitespace from pretty-printed XML is collapsed.
std::vector<LyricLine> readLines(QXmlStreamReader &xml)
{
    std::vector<LyricLine> out;
    LyricLine line;
    auto finish = [&] {
        line.text = line.text.simplified();
        line.chords = line.chords.simplified();
        if (!line.text.isEmpty())
            out.push_back(line);
        line = {};
    };
    int depth = 1;
    while (depth > 0 && !xml.atEnd()) {
        switch (xml.readNext()) {
        case QXmlStreamReader::StartElement: {
            const auto name = xml.name();
            if (name == QLatin1String("br")) {
                finish();
                xml.skipCurrentElement();
            } else if (name == QLatin1String("comment")) {
                xml.skipCurrentElement();
            } else if (name == QLatin1String("chord")) {
                const auto attrs = xml.attributes();
                QString chord = attrs.value(QStringLiteral("name")).toString();
                if (chord.isEmpty()) {
                    chord = attrs.value(QStringLiteral("root")).toString();
                    if (const auto bass = attrs.value(QStringLiteral("bass")); !bass.isEmpty())
                        chord += u'/' + bass.toString();
                }
                line.chords += u' ' + chord;
                ++depth; // chords may wrap text in OpenLyrics 0.9
            } else {
                ++depth; // formatting tags: keep their text
            }
            break;
        }
        case QXmlStreamReader::EndElement:
            --depth;
            break;
        case QXmlStreamReader::Characters:
            line.text += xml.text();
            break;
        default:
            break;
        }
    }
    finish();
    return out;
}

} // namespace

Result<ParsedSong> parseOpenLyrics(const QByteArray &bytes)
{
    QXmlStreamReader xml(bytes);
    if (!xml.readNextStartElement() || xml.name() != QLatin1String("song"))
        return invalid(QStringLiteral("Not an OpenLyrics file (no <song> element)"));

    ParsedSong song;
    QStringList orderNames;
    QStringList sectionNames;
    QString language; // the first language met; other languages are skipped

    while (!xml.atEnd()) {
        if (xml.readNext() != QXmlStreamReader::StartElement)
            continue;
        const auto name = xml.name();
        if (name == QLatin1String("title")) {
            const QString title = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (song.title.isEmpty())
                song.title = title;
        } else if (name == QLatin1String("author")) {
            const QString author = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
            if (!author.isEmpty())
                song.authors << author;
        } else if (name == QLatin1String("copyright")) {
            song.copyright = xml.readElementText(QXmlStreamReader::IncludeChildElements).simplified();
        } else if (name == QLatin1String("ccliNo")) {
            song.ccli = xml.readElementText().simplified();
        } else if (name == QLatin1String("key")) {
            song.key = xml.readElementText().simplified();
        } else if (name == QLatin1String("tempo")) {
            const QString type = xml.attributes().value(QStringLiteral("type")).toString();
            const QString value = xml.readElementText().simplified();
            song.tempo = type == QLatin1String("bpm") ? value + QStringLiteral(" bpm") : value;
        } else if (name == QLatin1String("verseOrder")) {
            orderNames = xml.readElementText().simplified().split(u' ', Qt::SkipEmptyParts);
        } else if (name == QLatin1String("verse")) {
            const QString verseName = xml.attributes().value(QStringLiteral("name")).toString();
            const QString lang = xml.attributes().value(QStringLiteral("lang")).toString();
            if (language.isEmpty())
                language = lang;
            const bool otherLanguage = !lang.isEmpty() && !language.isEmpty() && lang != language;
            const QString key = verseName.toLower();
            if (otherLanguage || sectionNames.contains(key)) {
                xml.skipCurrentElement();
                continue;
            }
            ParsedSection section{labelFromOpenLyricsName(verseName), {}};
            while (xml.readNextStartElement()) {
                if (xml.name() == QLatin1String("lines")) {
                    auto block = readLines(xml);
                    if (!block.empty())
                        section.blocks.push_back(std::move(block));
                } else {
                    xml.skipCurrentElement();
                }
            }
            if (section.blocks.empty()) {
                song.warnings << QStringLiteral("Verse \"%1\" has no lines").arg(verseName);
                continue;
            }
            sectionNames << key;
            song.sections.push_back(std::move(section));
        }
    }
    if (xml.hasError()) {
        return invalid(QStringLiteral("Line %1: %2").arg(xml.lineNumber()).arg(xml.errorString()));
    }
    if (song.sections.empty())
        return invalid(QStringLiteral("No lyrics found"));
    if (song.title.isEmpty())
        return invalid(QStringLiteral("The song has no title"));

    for (const QString &name : orderNames) {
        const qsizetype index = sectionNames.indexOf(name.toLower());
        if (index < 0)
            song.warnings << QStringLiteral("verseOrder names \"%1\", which is not in the song").arg(name);
        else
            song.order << song.sections[std::size_t(index)].label;
    }
    if (song.order.isEmpty()) {
        for (const ParsedSection &section : song.sections)
            song.order << section.label;
    }
    return song;
}

// ---- slides and search ------------------------------------------------------

std::vector<std::vector<LyricLine>> splitIntoSlides(const std::vector<LyricLine> &lines, int maxLines)
{
    std::vector<std::vector<LyricLine>> slides;
    const int total = int(lines.size());
    if (total == 0)
        return slides;
    const int limit = qMax(1, maxLines);
    const int count = (total + limit - 1) / limit;
    const int base = total / count;
    const int extra = total % count; // the first `extra` slides take one more line
    int next = 0;
    for (int i = 0; i < count; ++i) {
        const int size = base + (i < extra ? 1 : 0);
        slides.emplace_back(lines.begin() + next, lines.begin() + next + size);
        next += size;
    }
    return slides;
}

QString normaliseForSearch(const QString &text)
{
    return foldForSearch(text);
}

} // namespace lumen::core::songs
