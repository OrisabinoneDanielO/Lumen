#!/usr/bin/env python3
"""Writes the sample libraries used by tst_foreign_import (batch 17).

Run from anywhere: python3 make_libraries.py  (writes next to this script).
Standard library only.

The files follow each program's published or well-known layout:
  openlp/songs.sqlite          OpenLP 2.x/3.x (songs, authors, authors_songs;
                               lyrics as OpenLP's XML, verse_order "v1 c1 v2")
  easyworship/Databases/Data/  EasyWorship 6/7 (Songs.db: song; SongWords.db:
                               word, lyrics as RTF)
  propresenter/*.pro6, *.pro5  ProPresenter 6 and 5 XML documents
They are not copies of any real program's files: the hymn texts are in the
public domain and the Yoruba song was written for these tests.
"""

import base64
import os
import sqlite3
import uuid

HERE = os.path.dirname(os.path.abspath(__file__))


def fresh(path):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    if os.path.exists(path):
        os.remove(path)
    return sqlite3.connect(path)


def cdata(text):
    return "<![CDATA[" + text + "]]>"


# ---- OpenLP -------------------------------------------------------------------

def openlp():
    db = fresh(os.path.join(HERE, "openlp", "songs.sqlite"))
    db.executescript("""
        CREATE TABLE authors (id INTEGER PRIMARY KEY, first_name VARCHAR(128), last_name VARCHAR(128),
                              display_name VARCHAR(255) NOT NULL);
        CREATE TABLE songs (id INTEGER PRIMARY KEY, song_book_id INTEGER, title VARCHAR(255) NOT NULL,
                            alternate_title VARCHAR(255), lyrics TEXT NOT NULL, verse_order VARCHAR(128),
                            copyright VARCHAR(255), comments TEXT, ccli_number VARCHAR(64), song_number VARCHAR(64),
                            theme_name VARCHAR(128), search_title VARCHAR(255) NOT NULL, search_lyrics TEXT NOT NULL,
                            create_date DATETIME, last_modified DATETIME, temporary BOOLEAN);
        CREATE TABLE authors_songs (author_id INTEGER NOT NULL, song_id INTEGER NOT NULL, author_type VARCHAR(255));
        CREATE TABLE topics (id INTEGER PRIMARY KEY, name VARCHAR(128));
    """)
    authors = [(1, "Joseph", "Scriven", "Joseph M. Scriven"), (2, "Charles", "Converse", "Charles C. Converse"),
               (3, "Fanny", "Crosby", "Fanny Crosby"), (4, "Adé", "Ọlálékan", "Adé Ọlálékan")]
    db.executemany("INSERT INTO authors VALUES (?, ?, ?, ?)", authors)

    def lyrics(verses):
        body = "".join('<verse type="%s" label="%s">%s</verse>' % (t, n, cdata(text)) for t, n, text in verses)
        return '<?xml version="1.0" encoding="UTF-8"?><song version="1.0"><lyrics>%s</lyrics></song>' % body

    songs = [
        (1, "What a Friend We Have in Jesus", lyrics([
            ("v", "1", "What a friend we have in Jesus,\nAll our sins and griefs to bear!\n"
                       "What a privilege to carry\nEverything to God in prayer!"),
            ("v", "2", "Have we trials and temptations?\nIs there trouble anywhere?\n"
                       "We should never be discouraged;\nTake it to the Lord in prayer."),
        ]), "v1 v2", "Public domain", "", [1, 2]),
        (2, "Blessed Assurance", lyrics([
            ("v", "1", "Blessed assurance, Jesus is mine!\nO what a foretaste of glory divine!\n[---]\n"
                       "Heir of salvation, purchase of God,\nBorn of His Spirit, washed in His blood."),
            ("c", "1", "This is my story, this is my song,\nPraising my Savior all the day long;"),
            ("v", "2", "Perfect submission, perfect delight,\nVisions of rapture now burst on my sight;"),
        ]), "v1 c1 v2 c1", "Public domain", "22324", [3]),
        (3, "Ẹ Yin Olúwa", lyrics([
            ("v", "1", "Ẹ yin Olúwa, ẹ̀yin ènìyàn rẹ̀,\nỌlọ́run wa tóbi, ó ṣeun fún wa."),
            ("c", "1", "Ògo, ògo, ògo ni fún Ọba,\nỌlọ́run wa, a yìn ọ́ lónìí."),
            ("b", "1", "Ìfẹ́ rẹ̀ kò lópin,\nÀánú rẹ̀ dúró títí."),
        ]), "v1 c1 b1 c1", "© 2026 Lumen test song", "", [4]),
    ]
    for sid, title, text, order, copyright, ccli, author_ids in songs:
        db.execute("INSERT INTO songs (id, title, lyrics, verse_order, copyright, ccli_number, search_title, "
                   "search_lyrics, temporary) VALUES (?, ?, ?, ?, ?, ?, ?, ?, 0)",
                   (sid, title, text, order, copyright, ccli, title.lower(), ""))
        for aid in author_ids:
            db.execute("INSERT INTO authors_songs VALUES (?, ?, 'words')", (aid, sid))
    db.commit()
    db.close()


# ---- EasyWorship ----------------------------------------------------------------

def rtf_escape(text):
    """Text as RTF: Windows-1252 letters as \\'hh, others as \\uN?."""
    out = []
    for ch in text:
        code = ord(ch)
        if ch in "\\{}":
            out.append("\\" + ch)
        elif ch == "\n":
            out.append("\\par\r\n")
        elif code < 128:
            out.append(ch)
        else:
            try:
                byte = ch.encode("cp1252")
                out.append("\\'%02x" % byte[0])
            except UnicodeEncodeError:
                signed = code if code < 32768 else code - 65536
                out.append("\\u%d?" % signed)
    return "".join(out)


def rtf(text):
    return ("{\\rtf1\\ansi\\ansicpg1252\\deff0\\deflang1033{\\fonttbl{\\f0\\fnil\\fcharset0 Tahoma;}}"
            "{\\colortbl ;\\red255\\green255\\blue255;}"
            "{\\*\\generator EasyWorship test;}\\viewkind4\\uc1\\pard\\cf1\\f0\\fs56 "
            + rtf_escape(text) + "\\par\r\n}").encode("ascii")


def easyworship():
    data = os.path.join(HERE, "easyworship", "Databases", "Data")
    songs = fresh(os.path.join(data, "Songs.db"))
    songs.executescript("""
        CREATE TABLE song (rowid INTEGER PRIMARY KEY AUTOINCREMENT, song_item_uid TEXT, song_rev_uid TEXT,
                           song_uid TEXT, title TEXT, author TEXT, copyright TEXT, administrator TEXT,
                           description TEXT, tags TEXT, reference_number TEXT, vendor_id INTEGER,
                           presentation_id INTEGER, layout_revision INTEGER, revision INTEGER);
    """)
    words = fresh(os.path.join(data, "SongWords.db"))
    words.executescript("""
        CREATE TABLE word (rowid INTEGER PRIMARY KEY AUTOINCREMENT, song_id INTEGER, words TEXT,
                           slide_uids TEXT, slide_layout_revisions TEXT, slide_revisions TEXT);
    """)
    entries = [
        ("It Is Well with My Soul", "Horatio Spafford; Philip Bliss", "Public domain", "", "25376",
         "Verse 1\nWhen peace like a river attendeth my way,\nWhen sorrows like sea billows roll;\n\n"
         "Whatever my lot, Thou hast taught me to say,\nIt is well, it is well with my soul.\n\n"
         "Chorus\nIt is well with my soul,\nIt is well, it is well with my soul.\n\n"
         "Verse 2\nThough Satan should buffet, though trials should come,\n"
         "Let this blest assurance control,"),
        ("Café Praise", "Lumen tests", "© 2026 Lumen", "Lumen Music", "",
         "Verse 1\nSing in the café, sing in the street,\nPraise Him with every beat.\n\n"
         "Chorus\nẸ yin Olúwa — praise the Lord!\nSing it with one accord."),
    ]
    for title, author, copyright, admin, ccli, lyrics in entries:
        cur = songs.execute("INSERT INTO song (song_uid, title, author, copyright, administrator, reference_number) "
                            "VALUES (?, ?, ?, ?, ?, ?)", (str(uuid.uuid4()), title, author, copyright, admin, ccli))
        words.execute("INSERT INTO word (song_id, words) VALUES (?, ?)", (cur.lastrowid, rtf(lyrics)))
    songs.commit()
    words.commit()
    songs.close()
    words.close()


# ---- ProPresenter ----------------------------------------------------------------

def pro6(path, title, author, year, publisher, ccli, groups, arrangement):
    ids = {name: str(uuid.uuid4()).upper() for name, _ in groups}
    parts = []
    for name, slides in groups:
        slide_xml = []
        for text in slides:
            plain = base64.b64encode(text.encode("utf-8")).decode("ascii")
            rtf_data = base64.b64encode(rtf(text)).decode("ascii")
            slide_xml.append(
                '<RVDisplaySlide backgroundColor="0 0 0 0" enabled="true" UUID="%s">'
                '<array rvXMLIvarName="cues"/><array rvXMLIvarName="displayElements">'
                '<RVTextElement displayName="Default" UUID="%s" typeID="0">'
                '<RVRect3D rvXMLIvarName="position">{0 0 0 1920 1080}</RVRect3D>'
                '<NSString rvXMLIvarName="PlainText">%s</NSString>'
                '<NSString rvXMLIvarName="RTFData">%s</NSString>'
                '</RVTextElement></array></RVDisplaySlide>' % (uuid.uuid4(), uuid.uuid4(), plain, rtf_data))
        parts.append('<RVSlideGrouping name="%s" uuid="%s" color="0 0 1 1"><array rvXMLIvarName="slides">%s</array>'
                     '</RVSlideGrouping>' % (name, ids[name], "".join(slide_xml)))
    order = "".join("<NSString>%s</NSString>" % ids[name] for name in arrangement)
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<RVPresentationDocument height="1080" width="1920" versionNumber="600" docType="0" '
           'CCLISongTitle="%s" CCLIAuthor="%s" CCLICopyrightYear="%s" CCLIPublisher="%s" CCLISongNumber="%s" '
           'CCLIArtistCredits="" CCLIDisplay="false" category="Song">'
           '<RVTimeline duration="0" loop="false" rvXMLIvarName="timeline"/>'
           '<array rvXMLIvarName="groups">%s</array>'
           '<array rvXMLIvarName="arrangements"><RVSongArrangement name="Sunday" uuid="%s" color="0 0 0 0">'
           '<array rvXMLIvarName="groupIDs">%s</array></RVSongArrangement></array>'
           '</RVPresentationDocument>\n') % (title, author, year, publisher, ccli, "".join(parts), uuid.uuid4(), order)
    with open(path, "w", encoding="utf-8") as f:
        f.write(xml)


def pro5(path, title, author, groups):
    parts = []
    for name, slides in groups:
        slide_xml = "".join('<RVDisplaySlide enabled="1" UUID="%s"><displayElements>'
                            '<RVTextElement displayName="Default" RTFData="%s"/></displayElements></RVDisplaySlide>'
                            % (uuid.uuid4(), base64.b64encode(rtf(text)).decode("ascii")) for text in slides)
        parts.append('<RVSlideGrouping name="%s" uuid="%s"><slides>%s</slides></RVSlideGrouping>'
                     % (name, str(uuid.uuid4()).upper(), slide_xml))
    xml = ('<?xml version="1.0" encoding="utf-8"?>\n'
           '<RVPresentationDocument height="768" width="1024" versionNumber="500" CCLISongTitle="%s" CCLIAuthor="%s">'
           '<groups>%s</groups><arrangements/></RVPresentationDocument>\n') % (title, author, "".join(parts))
    with open(path, "w", encoding="utf-8") as f:
        f.write(xml)


def propresenter():
    folder = os.path.join(HERE, "propresenter")
    os.makedirs(folder, exist_ok=True)
    pro6(os.path.join(folder, "Come Thou Fount.pro6"), "Come, Thou Fount of Every Blessing", "Robert Robinson",
         "1758", "Public domain", "108389",
         [("Verse 1", ["Come, Thou Fount of every blessing,\nTune my heart to sing Thy grace;",
                       "Streams of mercy, never ceasing,\nCall for songs of loudest praise."]),
          ("Chorus", ["Praise the mount! I'm fixed upon it,\nMount of Thy redeeming love."]),
          ("Verse 2", ["Here I raise my Ebenezer;\nHither by Thy help I'm come;"])],
         ["Verse 1", "Chorus", "Verse 2", "Chorus"])
    pro6(os.path.join(folder, "Ọlọrun Tobi.pro6"), "Ọlọ́run Tóbi", "Lumen tests", "2026", "Lumen", "",
         [("Verse 1", ["Ọlọ́run tóbi, Ọlọ́run ńlá,\nKò sí ẹni tó dà bí Rẹ."]),
          ("Tag", ["Àmín, àmín."])],
         ["Verse 1", "Tag"])
    pro5(os.path.join(folder, "Nearer My God.pro5"), "Nearer, My God, to Thee", "Sarah Flower Adams",
         [("Verse 1", ["Nearer, my God, to Thee,\nNearer to Thee!",
                       "E'en though it be a cross\nThat raiseth me,"])])
    # ProPresenter 7 documents are binary (protocol buffers); a stand-in.
    with open(os.path.join(folder, "Modern Song.pro"), "wb") as f:
        f.write(b"\x0a\x0c\x08\x07\x10\x00\x18\x00binary")


openlp()
easyworship()
propresenter()
print("wrote", HERE)
