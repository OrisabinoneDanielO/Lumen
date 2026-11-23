# The .lumen file format

A `.lumen` file carries a service plan and everything it needs to another computer. It is an ordinary zip archive:

| Entry | What it holds |
| --- | --- |
| `lumen.json` | The plan, its presentations and a list of media (below) |
| `media/<n>-<name>` | Each media file the slides use (videos, pictures, imported slide pictures) |
| `media/<n>-thumb-<name>` | Its thumbnail, if it has one |

## lumen.json (version 1)

```json
{
  "format": "lumen",
  "version": 1,
  "app": "Lumen Presenter 0.1.0",
  "plan": {
    "name": "Sunday Service",
    "date": "2026-11-29",
    "items": [
      {"header": "Worship", "notes": ""},
      {"presentation": "<id>", "arrangement": "<id>", "header": "", "notes": "Key of G"}
    ]
  },
  "presentations": [
    {
      "id": "<id>", "kind": "song", "title": "Amazing Grace", "theme": "Lyrics",
      "metadata": {"author": "John Newton"},
      "groups": [
        {"id": "<id>", "label": "Verse 1", "colour": "", "hotkey": "",
         "slides": [{"notes": "", "chords": "", "transition": {},
                     "elements": [{"type": "text", "frame": [0, 0, 1920, 1080], "style": {}, "z": 0,
                                   "content": "Amazing grace! How sweet the sound", "media": "<media id>"}]}]}
      ],
      "arrangements": [{"id": "<id>", "name": "Default", "default": true, "groups": ["<group id>"]}],
      "fingerprint": "<sha256 of kind, title, labels and contents>"
    }
  ],
  "media": [
    {"id": "<id>", "file": "media/1-loop.webm", "thumbnail": "media/1-thumb-loop.jpg", "hash": "sha256:...",
     "durationMs": 10000, "loop": true, "trimInMs": 0, "trimOutMs": 0}
  ]
}
```

- Ids only link entries inside the file; importing gives everything new ids.
- `kind` is `song`, `scripture`, `slides` or `media`; element `type` is `text`, `image`, `video`, `shape`, `timer` or `clock`.
- Themes are referred to by name. If the importing library has no theme of that name, the default is used and the import report says so.
- A newer Lumen may add fields; readers ignore fields they do not know. A file with a higher `version` than the reader knows is refused with a message to update.

## Importing

Importing always adds a new plan. A presentation already in the library (same kind, title, group labels and contents) is reused instead of copied, so importing next week's plan does not duplicate the songs. A media file whose content hash is already in the library is reused; others are copied into the library's media folder, in a folder named after the `.lumen` file.
