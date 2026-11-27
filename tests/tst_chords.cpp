#include "lumen/core/songs/chords.h"
#include "lumen/core/songs/songimport.h"

#include <QTest>

using namespace lumen::core::songs;

// Chords in lyrics (the design doc's "chords and transposition"): kept
// inline, never in the audience's text, moved up and down for the band.
class TestChords : public QObject
{
    Q_OBJECT

private slots:
    void recognisesChords()
    {
        for (const char *name : {"G", "Em", "Em7", "F#m7b5", "Csus4", "G/B", "Bb(add9)", "Dmaj7", "Ab", "C#dim"})
            QVERIFY2(isChord(QString::fromLatin1(name)), name);
        for (const char *name : {"", "Bridge", "Chorus", "H", "Go", "g", "Verse 1"})
            QVERIFY2(!isChord(QString::fromLatin1(name)), name);
        QVERIFY(isChordLine(QStringLiteral("  G        C    D  ")));
        QVERIFY(!isChordLine(QStringLiteral("Go tell it on the mountain")));
        QVERIFY(!isChordLine(QString()));
    }

    void splitsInlineChords()
    {
        const ChordedLine line = splitChords(QStringLiteral("[G]Amazing [C]grace how [G]sweet"));
        QCOMPARE(line.text, QStringLiteral("Amazing grace how sweet"));
        QCOMPARE(line.chordPro, QStringLiteral("[G]Amazing [C]grace how [G]sweet"));
        // Brackets that are not chords stay in the words.
        const ChordedLine plain = splitChords(QStringLiteral("Sing [softly] to the Lord"));
        QCOMPARE(plain.text, QStringLiteral("Sing [softly] to the Lord"));
        QVERIFY(plain.chordPro.isEmpty());
    }

    void mergesChordsAboveWords()
    {
        QCOMPARE(mergeChordLine(QStringLiteral("G       C         G"), QStringLiteral("Amazing grace how sweet")),
                 QStringLiteral("[G]Amazing [C]grace how [G]sweet"));
        // A chord after the last word.
        QCOMPARE(mergeChordLine(QStringLiteral("D          A"), QStringLiteral("Be still")),
                 QStringLiteral("[D]Be still   [A]"));
    }

    void transposes()
    {
        QCOMPARE(transposeChord(QStringLiteral("G"), 2, false), QStringLiteral("A"));
        QCOMPARE(transposeChord(QStringLiteral("G/B"), 2, false), QStringLiteral("A/C#"));
        QCOMPARE(transposeChord(QStringLiteral("Em7"), -2, false), QStringLiteral("Dm7"));
        QCOMPARE(transposeChord(QStringLiteral("C"), 3, true), QStringLiteral("Eb"));
        QCOMPARE(transposeChord(QStringLiteral("F#m7b5"), 1, false), QStringLiteral("Gm7b5"));
        QCOMPARE(transposeChord(QStringLiteral("B"), 1, false), QStringLiteral("C"));
        QCOMPARE(transposeChord(QStringLiteral("D"), 12, false), QStringLiteral("D"));
        QCOMPARE(transposeChordPro(QStringLiteral("[G]Amazing [C]grace [softly]"), 2, false),
                 QStringLiteral("[A]Amazing [D]grace [softly]"));
        QCOMPARE(transposeKey(QStringLiteral("G"), 2), QStringLiteral("A"));
        QCOMPARE(transposeKey(QStringLiteral("G"), 3), QStringLiteral("Bb")); // not A#
        QCOMPARE(transposeKey(QStringLiteral("C"), 1), QStringLiteral("Db"));
        QCOMPARE(transposeKey(QStringLiteral("E"), 2), QStringLiteral("F#"));
        QCOMPARE(transposeKey(QStringLiteral("Em"), 1), QStringLiteral("Fm"));
        QVERIFY(keyUsesFlats(QStringLiteral("Bb")) && !keyUsesFlats(QStringLiteral("D")));
    }

    // Plain-text songs: inline chords and chords above the words come off
    // the audience's text and are kept with the line.
    void textImportKeepsChordsApart()
    {
        const auto song = parseTextSong(QStringLiteral("Amazing Grace\n\n[Verse 1]\n"
                                                       "[G]Amazing [C]grace how [G]sweet the sound\n"
                                                       "     D\nThat saved a wretch like me\n"
                                                       "G   C   D\n\n[Chorus]\nNo chords here"),
                                        QStringLiteral("x"));
        QVERIFY(song.ok());
        const auto &verse = song.value().sections.front().blocks.front();
        QCOMPARE(int(verse.size()), 2); // the chords-only last line is left out
        QCOMPARE(verse[0].text, QStringLiteral("Amazing grace how sweet the sound"));
        QCOMPARE(verse[0].chords, QStringLiteral("[G]Amazing [C]grace how [G]sweet the sound"));
        QCOMPARE(verse[1].text, QStringLiteral("That saved a wretch like me"));
        QCOMPARE(verse[1].chords, QStringLiteral("That [D]saved a wretch like me"));
        QCOMPARE(slideChords(verse), QStringLiteral("[G]Amazing [C]grace how [G]sweet the sound\nThat [D]saved a wretch like me"));
        const auto &chorus = song.value().sections.back().blocks.front();
        QVERIFY(chorus.front().chords.isEmpty());
        QVERIFY(slideChords(chorus).isEmpty());
    }

    void openLyricsKeepsWhereChordsFall()
    {
        const auto song = parseOpenLyrics(QByteArrayLiteral(
            "<song xmlns=\"http://openlyrics.info/namespace/2009/song\" version=\"0.8\"><properties><titles><title>T</title></titles></properties>"
            "<lyrics><verse name=\"v1\"><lines><chord name=\"G\"/>Joyful, joyful, <chord name=\"C\"/>we adore Thee,<br/>God of glory</lines></verse></lyrics></song>"));
        QVERIFY(song.ok());
        const auto &lines = song.value().sections.front().blocks.front();
        QCOMPARE(lines[0].text, QStringLiteral("Joyful, joyful, we adore Thee,"));
        QCOMPARE(lines[0].chords, QStringLiteral("[G]Joyful, joyful, [C]we adore Thee,"));
        QVERIFY(lines[1].chords.isEmpty());
    }
};

QTEST_MAIN(TestChords)
#include "tst_chords.moc"
