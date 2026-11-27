# Free songs

The songs every Lumen library starts with: about 1,200 hymns and songs, with their chords, that may be shared freely. They are in `free/songs.json` (built into Lumen) and are added to a library the first time a new version of the collection starts; songs a library already has are left alone, and a church can delete any it does not want.

| Source | Songs | Taken | Left out |
| --- | --- | --- | --- |
| [WorshipCommons](https://github.com/ChurchApps/WorshipCommonsContent) (ChurchApps) | 1,206 | Public domain (`PD`, 1,177), Creative Commons Attribution (`CC-BY`, 28) and Attribution-ShareAlike (`CC-BY-SA`, 1), each as the song's own record says | The WorshipCommons License (free for worship, but not for shipping in an app) and songs shared under one writer's own permissions |
| [OpenLP song collection](https://github.com/Jesulayomy/OpenLP) (Jesulayomy) | 6 | Songs whose every author is named with a death year before 1956 (public domain 70 years after death, everywhere) and that carry no copyright notice; 132 more were already in WorshipCommons | Everything else: songs by living or recent writers (Hillsong, Bethel, Matt Redman, Chris Tomlin and others are copyrighted) and "Author Unknown" songs, which cannot be checked |

Not used: [open-hymnal-json](https://github.com/h1rdr3v2/open-hymnal-json). Its MIT licence covers the repository, but its hymns mix public-domain texts with copyrighted ones (for example "How Great Thou Art") and give no authors to tell them apart; its public-domain classics are in WorshipCommons.

## Credit

Each song's footer carries its credit, shown on the slides: "Public domain", or for Creative Commons songs the writer, the licence and "worshipcommons.org", as those licences require. A few public-domain sources ask for credit too; their line is kept.

WorshipCommons notes that its licensing decisions are made in good faith and may contain errors, and that copyright terms vary by country. If a song should not be here, report it and it will be removed.

## Rebuilding

`tools/songs/build_free_songs.py` builds `free/songs.json` from a downloaded WorshipCommons `catalog.json` and the OpenLP collection's `Songs` folder (nothing is fetched by the script):

```
python3 -I tools/songs/build_free_songs.py --worshipcommons catalog.json \
    --openlp OpenLP/Songs --out songs/free/songs.json
```

Raise `version` in the file (the script writes 1) when the collection changes, so libraries pick up the new songs. Checked 2026-11-27.
