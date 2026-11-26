# Contributing to Lumen Presenter

Thank you for helping. Lumen is free software for churches; every bug report, test on real equipment, fix and translation makes Sunday morning easier for someone.

## Ways to help

- **Report bugs and ideas** in GitHub issues (there are templates). For bugs, say what you did, what you expected, what happened, and your OS, Qt version and screens.
- **Test** in a rehearsal or a real service, on Windows or macOS, with MIDI controllers, NDI, phones, multi-screen setups, or song libraries from other programs. [docs/status.md](docs/status.md) lists what has not been tried yet.
- **Code**: fix a bug, finish something listed in [docs/status.md](docs/status.md), or propose a feature (open an issue first for anything large, so we can agree on the approach).
- **Translate** the interface, or improve the [user guide](docs/user-guide.md).

## Getting started

1. Read the [README](README.md), then [docs/architecture.md](docs/architecture.md) for how the code fits together.
2. Build and run the tests: [docs/building.md](docs/building.md).
3. Run `lumen --design-preview` to try the app with a sample service.

## The rules that keep services safe

These come from the design doc and are not up for casual change (ask in an issue first):

- **The operator window must never be able to freeze an output.** Outputs are separate processes; anything that draws, decodes or counts time for the audience belongs in the render process.
- **Offline and free.** No accounts, licence keys, tiers, watermarks or telemetry. No network use except user-started imports, optional update checks and local-network features (which are off until turned on). `tests/offline_check.sh` checks this.
- **Nothing reaches the screen by accident.** Edit mode never goes live; remote commands follow the same rules as the keyboard.
- **Data is never lost silently.** Changes are saved as they happen, in transactions; errors are returned (`Result`, `Status`), not thrown.

## How changes are made

- **One topic per pull request**, on a branch (`fix-...`, `feature-...`).
- **Tests come with the change.** A bug fix gets a test that failed before the fix. A feature gets tests for what it promises.
- **Never weaken or delete an existing test to make a change pass.** If an interface really must change, update its tests in the same pull request and say why in the description.
- **Unfinished features stay behind a setting that is off by default**, so `main` can always run a service.
- **Build and run all the tests** (`ctest --preset debug`) before asking for review; CI runs them on Windows, macOS and Linux.
- **Write down decisions** that someone will later wonder about in [docs/decisions.md](docs/decisions.md), and update the [user guide](docs/user-guide.md) for anything a user will see.

## Code style

- C++20 and Qt 6. Follow the style of the code around you: four-space indents, `m_` members, Qt types (`QString`, `QList`) at Qt boundaries, `std::` containers inside.
- Comments say *why*, in plain sentences; the code says *what*.
- Interface text is plain and short, in sentence case, and wrapped in `qsTr()` / `tr()` for translation. Say what will happen ("Restoring replaces the library…"), not how it works inside.
- The operator window follows `design/mockups/*.html`: its regions, colours and labels.
- No new dependency without discussing it: everything Lumen depends on must work on all three systems and ship in the installers. Licences must be compatible with GPL-3.0.

## Commits and pull requests

- Commit messages: a short first line saying what changed, then a blank line and why, if it is not obvious.
- In the pull request, say what changed, how you tested it (which tests, which hardware), and anything you could not test.
- Screenshots help for anything visible.

## Licence

Lumen Presenter is GPL-3.0. By contributing you agree that your contribution is licensed under the same terms.

## Questions

Open an issue or a discussion. There are no silly questions; Lumen should be easy to work on.
