# Translations

Lumen's operator window can be shown in other languages: **Plan ▾ → Language…**. It starts in the computer's language when Lumen has it, otherwise English, and changes at once without a restart.

| File | Language | State |
| --- | --- | --- |
| `lumen_fr.ts` | French (Français) | Finished |
| `lumen_es.ts` | Spanish (Español) | Finished |
| `lumen_pt.ts` | Portuguese (Português) | Finished |
| `lumen_yo.ts` | Yoruba (Yorùbá) | **Needs a translator** |
| `lumen_ig.ts` | Igbo | **Needs a translator** |
| `lumen_ha.ts` | Hausa | **Needs a translator** |
| `lumen_sw.ts` | Swahili (Kiswahili) | **Needs a translator** |

The French, Spanish and Portuguese translations were written for the project and have not yet been reviewed by a native-speaking church volunteer; corrections are very welcome.

## Help translate

Yoruba, Igbo, Hausa and Swahili are waiting for native speakers. Machine translation reads badly to the volunteers who run a service, so these languages are only offered once a person has translated and checked them. Until then, `experimental/draftLanguages = true` in Lumen's settings shows them for testing.

1. Install **Qt Linguist** (part of the free Qt tools; on Fedora `qt6-linguist`, on Debian and Ubuntu `qt6-tools-dev-tools`).
2. Open the language's file, for example `lumen_yo.ts`, in Qt Linguist. Each line of the interface is shown in English; type the translation and mark it done (Ctrl+Enter).
3. Keep `%1`, `%2` and `%n` (they are filled in by Lumen), key names (F1, Space, Enter, Ctrl+B) and names like CCLI, MIDI and PowerPoint as they are.
4. Keep button labels short: the window can be as narrow as 1024 pixels.
5. Send the file back as a pull request, or attach it to an issue.

The words an operator sees during a service matter most: the toolbar (Clear, Logo, Black), Preview and Live, the service plan and the scripture window.

## For developers

The `.ts` files are made from the source code with `cmake --build build/debug --target update_translations` (Qt's `lupdate`), and compiled into Lumen when it is built (Qt's `lrelease`; without Qt's LinguistTools Lumen is built in English only). Wrap every text the operator reads in `qsTr()` (QML) or `tr()` (C++), and run `update_translations` after adding some. A language is offered when its file is listed in `app/operator/language.cpp` as finished.
