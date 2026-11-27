#include "lumen/core/songs/chords.h"

#include <QHash>
#include <QRegularExpression>

#include <array>

namespace lumen::core::songs {

namespace {

const QRegularExpression &chordPattern()
{
    static const QRegularExpression pattern(QStringLiteral(
        R"(^([A-G])([#b]?)((?:maj|min|dim|aug|sus|add|m|M|[0-9]|\+|-|\(|\)|#|b|°|ø)*)(?:/([A-G])([#b]?))?$)"));
    return pattern;
}

const QRegularExpression &inlineChord()
{
    static const QRegularExpression pattern(QStringLiteral(R"(\[([^\[\]\s]{1,12})\])"));
    return pattern;
}

constexpr std::array<const char *, 12> sharpNames{"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
constexpr std::array<const char *, 12> flatNames{"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};

int pitchOf(const QString &letter, const QString &accidental)
{
    static const QHash<QChar, int> base{{u'C', 0}, {u'D', 2}, {u'E', 4}, {u'F', 5}, {u'G', 7}, {u'A', 9}, {u'B', 11}};
    int pitch = base.value(letter.at(0));
    if (accidental == QLatin1String("#"))
        ++pitch;
    else if (accidental == QLatin1String("b"))
        --pitch;
    return (pitch + 12) % 12;
}

QString noteName(int pitch, bool flats)
{
    pitch = ((pitch % 12) + 12) % 12;
    return QString::fromLatin1(flats ? flatNames[std::size_t(pitch)] : sharpNames[std::size_t(pitch)]);
}

} // namespace

bool isChord(const QString &name)
{
    return !name.isEmpty() && chordPattern().match(name).hasMatch();
}

ChordedLine splitChords(const QString &line)
{
    ChordedLine out;
    bool found = false;
    QString text;
    qsizetype from = 0;
    auto it = inlineChord().globalMatch(line);
    while (it.hasNext()) {
        const auto m = it.next();
        if (!isChord(m.captured(1)))
            continue;
        found = true;
        text += line.mid(from, m.capturedStart() - from);
        from = m.capturedEnd();
    }
    text += line.mid(from);
    out.text = found ? text.simplified() : line;
    if (found)
        out.chordPro = line.trimmed();
    return out;
}

bool isChordLine(const QString &line)
{
    const QStringList tokens = line.split(QRegularExpression(QStringLiteral(R"(\s+)")), Qt::SkipEmptyParts);
    if (tokens.isEmpty())
        return false;
    for (const QString &token : tokens) {
        if (!isChord(token))
            return false;
    }
    return true;
}

QString mergeChordLine(const QString &chords, const QString &lyrics)
{
    // Chord start columns, applied from the right so earlier ones stay put.
    QList<std::pair<qsizetype, QString>> placed;
    static const QRegularExpression token(QStringLiteral(R"(\S+)"));
    auto it = token.globalMatch(chords);
    while (it.hasNext()) {
        const auto m = it.next();
        placed.append({m.capturedStart(), m.captured()});
    }
    QString merged = lyrics;
    for (auto p = placed.crbegin(); p != placed.crend(); ++p) {
        // A chord past the end of the words (an ending chord) goes after them.
        if (p->first > merged.size())
            merged += QString(p->first - merged.size(), u' ');
        merged.insert(p->first, u'[' + p->second + u']');
    }
    return merged.trimmed();
}

QString transposeChord(const QString &chord, int semitones, bool flats)
{
    const auto m = chordPattern().match(chord);
    if (!m.hasMatch() || semitones % 12 == 0)
        return chord;
    QString out = noteName(pitchOf(m.captured(1), m.captured(2)) + semitones, flats) + m.captured(3);
    if (!m.captured(4).isEmpty())
        out += u'/' + noteName(pitchOf(m.captured(4), m.captured(5)) + semitones, flats);
    return out;
}

QString transposeChordPro(const QString &text, int semitones, bool flats)
{
    if (semitones % 12 == 0)
        return text;
    QString out;
    qsizetype from = 0;
    auto it = inlineChord().globalMatch(text);
    while (it.hasNext()) {
        const auto m = it.next();
        out += text.mid(from, m.capturedStart() - from);
        out += isChord(m.captured(1)) ? u'[' + transposeChord(m.captured(1), semitones, flats) + u']' : m.captured();
        from = m.capturedEnd();
    }
    return out + text.mid(from);
}

bool keyUsesFlats(const QString &key)
{
    // F# rather than Gb, as bands usually write it.
    static const QStringList flatKeys{"F", "Bb", "Eb", "Ab", "Db", "Dm", "Gm", "Cm", "Fm", "Bbm", "Ebm"};
    return flatKeys.contains(key.trimmed());
}

QString transposeKey(const QString &key, int semitones)
{
    const auto m = chordPattern().match(key.trimmed());
    if (!m.hasMatch() || semitones % 12 == 0)
        return key;
    const QString minor = m.captured(3).startsWith(u'm') && !m.captured(3).startsWith(QLatin1String("maj")) ? QStringLiteral("m") : QString();
    const int pitch = pitchOf(m.captured(1), m.captured(2)) + semitones;
    // Spell the new key the way it is usually written.
    const QString sharp = noteName(pitch, false) + minor;
    const QString flat = noteName(pitch, true) + minor;
    if (sharp == flat)
        return sharp;
    return keyUsesFlats(flat) ? flat : sharp;
}

} // namespace lumen::core::songs
