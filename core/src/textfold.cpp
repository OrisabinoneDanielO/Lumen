#include "lumen/core/textfold.h"

namespace lumen::core {

QString foldForSearch(const QString &text)
{
    const QString decomposed = text.normalized(QString::NormalizationForm_KD);
    QString out;
    out.reserve(decomposed.size());
    for (const QChar c : decomposed) {
        const auto category = c.category();
        if (category == QChar::Mark_NonSpacing || category == QChar::Mark_SpacingCombining
            || category == QChar::Mark_Enclosing)
            continue;
        QChar folded = c.toCaseFolded();
        switch (folded.unicode()) {
        case 0x0253: folded = u'b'; break; // ɓ
        case 0x0257: folded = u'd'; break; // ɗ
        case 0x0199: folded = u'k'; break; // ƙ
        case 0x01B4: folded = u'y'; break; // ƴ
        case 0x0131: folded = u'i'; break; // dotless i
        default: break;
        }
        out += folded.isLetterOrNumber() ? folded : QChar(u' ');
    }
    return out.simplified();
}

} // namespace lumen::core
