# Plugins

Reserved for integrations that need a vendor's software kit (SongSelect, DeckLink). Nothing lives here yet:

- NDI output loads the user's NDI Runtime at run time from `render/ndisink.cpp` (no SDK needed to build).
- MIDI, OSC and the HTTP API are part of the operator app (`app/operator/midiinput.*`, `oscserver.*`, `remoteserver.*`).
- SongSelect waits for the CCLI partner agreement; DeckLink needs Blackmagic's SDK (see `docs/decisions.md`, batch 15).
