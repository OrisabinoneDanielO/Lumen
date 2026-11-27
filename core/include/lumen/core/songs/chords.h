#pragma once

// Chords in lyrics. Lumen keeps them ChordPro style, inline before the
// syllable they fall on: "[G]Amazing [C]grace". The audience never sees
// them; the stage display shows them, transposed if the song is.

#include <QString>
#include <QStringList>

namespace lumen::core::songs {

// "G", "Em7", "F#m7b5", "Csus4", "G/B", "Bb(add9)"; not "Bridge" or "Chorus".
bool isChord(const QString &name);

// A lyric line with inline chords: its text without them, and the line as
// written (empty if it had no chords).
struct ChordedLine
{
    QString text;
    QString chordPro;
};
ChordedLine splitChords(const QString &line);

// A line of nothing but chords ("G    C    D"), as written above lyrics.
bool isChordLine(const QString &line);
// Chords written above a lyric line, placed by column into it:
// ("G       C", "Amazing grace") -> "[G]Amazing [C]grace".
QString mergeChordLine(const QString &chords, const QString &lyrics);

// Moves a chord up (positive) or down by semitones, keeping its kind and
// bass note: ("G/B", 2) -> "A/C#". Sharps or flats as `flats` says.
QString transposeChord(const QString &chord, int semitones, bool flats);
// Every [chord] in ChordPro text.
QString transposeChordPro(const QString &text, int semitones, bool flats);
// A key ("G", "Em", "Bb") moved by semitones; the result's own spelling
// decides sharps or flats ("F" and "Bb" use flats, "D" sharps).
QString transposeKey(const QString &key, int semitones);
// Whether a key is written with flats (F, Bb, Eb, Ab, Db; Dm, Gm, Cm, Fm,
// Bbm, Ebm). F# is written with sharps.
bool keyUsesFlats(const QString &key);

} // namespace lumen::core::songs
