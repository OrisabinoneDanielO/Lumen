#pragma once

#include <QString>

namespace lumen::core {

// Text folded for matching what people type: case-folded, accents and tone
// marks removed (Yoruba ẹ -> e), Hausa hooked letters plain (ɗ -> d),
// punctuation as single spaces. Used by song search and Bible book names.
QString foldForSearch(const QString &text);

} // namespace lumen::core
