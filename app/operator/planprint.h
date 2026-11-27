#pragma once

#include "servicedata.h"

#include "lumen/core/result.h"

#include <QString>

namespace lumen::app {

// The plan on paper (EasyWorship prints these): a PDF the operator prints
// from the system's viewer or keeps.
//   ServiceOrder: the items in order, with their kind, notes, key and
//                 credit, and the headers dividing the service.
//   Lyrics:       each song's words, section by section (and its order),
//                 and each scripture passage, for the musicians or a
//                 printed handout.
//   ChordCharts:  each song with its chords above the words.
enum class PrintKind { ServiceOrder, Lyrics, ChordCharts };

// The document as HTML (also what the tests check).
QString planHtml(const PlanView &plan, PrintKind kind);
// Writes it to a PDF file (A4 or US Letter, as the system's locale prefers).
core::Status writePlanPdf(const PlanView &plan, PrintKind kind, const QString &file);

} // namespace lumen::app
