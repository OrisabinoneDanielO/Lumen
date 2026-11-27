#!/usr/bin/env python3
"""Builds songs/free/songs.json: the free songs every Lumen library starts with.

Sources (both downloaded by hand, then passed in; nothing is fetched here):
  --worshipcommons catalog.json from github.com/ChurchApps/WorshipCommonsContent
      Songs licensed PD, CC-BY or CC-BY-SA are taken (not the WorshipCommons
      licence, which covers worship use but not shipping in an app, and not
      writer-specific permissions).
  --openlp folder of OpenLyrics files (Songs/ of github.com/Jesulayomy/OpenLP)
      Only songs where every author is named with a death year before 1956
      (so public domain for 70 years after death, everywhere) and no
      copyright notice is given. "Author Unknown" and modern songs are left
      out.

Run: python3 -I tools/songs/build_free_songs.py --worshipcommons catalog.json
         --openlp OpenLP/Songs --out songs/free/songs.json
"""
import argparse
import json
import os
import re
import sys
import unicodedata
import xml.etree.ElementTree as ET

PD_DEATH_BEFORE = 1956
NS = {"ol": "http://openlyrics.info/namespace/2009/song"}


def fold(title):
    t = unicodedata.normalize("NFKD", title).encode("ascii", "ignore").decode().lower()
    return re.sub(r"[^a-z0-9]+", " ", t).strip()


def worshipcommons(path):
    rows = json.load(open(path, encoding="utf-8"))["rows"]
    out = []
    for r in rows:
        licence = r.get("license")
        if licence not in ("PD", "CC-BY", "CC-BY-SA") or not (r.get("chordPro") or "").strip():
            continue
        writer = (r.get("writer") or "").strip()
        if licence == "PD":
            credit = "Public domain"
            if r.get("attribution") and r["attribution"] != writer:
                credit += " · " + r["attribution"]
        else:
            version = r.get("licenseVersion") or ""
            name = licence.replace("CC-BY-SA", "CC BY-SA").replace("CC-BY", "CC BY")
            credit = f"{writer} · {name} {version.split()[-1] if version else '4.0'} · worshipcommons.org".strip()
        out.append({
            "id": "wc-" + r["id"],
            "source": "WorshipCommons",
            "title": r["title"].strip(),
            "writer": writer,
            "language": r.get("language") or "",
            "key": r.get("songKey") or "",
            "ccli": str(r["ccli"]) if r.get("ccli") else "",
            "licence": licence,
            "copyright": credit,
            "format": "text",
            "body": r["chordPro"].replace("\r\n", "\n"),
        })
    return out


def openlp(folder):
    out = []
    for name in sorted(os.listdir(folder)):
        if not name.endswith(".xml"):
            continue
        path = os.path.join(folder, name)
        try:
            root = ET.parse(path).getroot()
        except ET.ParseError:
            continue
        props = root.find("ol:properties", NS)
        if props is None:
            continue
        if props.find("ol:copyright", NS) is not None:
            continue
        authors = [a.text.strip() for a in props.findall("ol:authors/ol:author", NS) if a.text and a.text.strip()]
        if not authors:
            continue
        deaths = []
        for a in authors:
            m = re.search(r"\((?:c(?:irca|\.)?\s*)?\d{3,4}\s*-\s*(?:c(?:irca|\.)?\s*)?(\d{3,4})\)", a)
            if not m:
                deaths = None
                break
            deaths.append(int(m.group(1)))
        if not deaths or max(deaths) >= PD_DEATH_BEFORE:
            continue
        title = props.findtext("ol:titles/ol:title", default="", namespaces=NS).strip()
        if not title:
            continue
        # Words to show (an instrumental piece such as "Canon in D" has none).
        if not any((l.text or "").strip() or len(l) for l in root.iter("{%s}lines" % NS["ol"])):
            continue
        writers = []
        for a in authors:
            plain = re.sub(r"\s*\(.*?\)", "", a).strip()
            if "," in plain:  # "Luther, Martin" -> "Martin Luther"
                last, first = plain.split(",", 1)
                plain = f"{first.strip()} {last.strip()}"
            if plain not in writers:
                writers.append(plain)
        out.append({
            "id": "olp-" + fold(title).replace(" ", "-")[:60],
            "source": "OpenLP song collection",
            "title": title,
            "writer": ", ".join(writers),
            "language": "English",
            "key": "",
            "ccli": "",
            "licence": "PD",
            "copyright": "Public domain",
            "format": "openlyrics",
            "body": open(path, encoding="utf-8").read(),
        })
    return out


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--worshipcommons", required=True)
    p.add_argument("--openlp", required=True)
    p.add_argument("--out", required=True)
    a = p.parse_args()
    songs = worshipcommons(a.worshipcommons)
    seen = {(fold(s["title"]), s["language"]) for s in songs}
    added = 0
    for s in openlp(a.openlp):
        key = (fold(s["title"]), s["language"])
        if key in seen:
            continue  # WorshipCommons has it (with chords)
        seen.add(key)
        songs.append(s)
        added += 1
    songs.sort(key=lambda s: (s["language"] != "English", s["language"], fold(s["title"])))
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump({"version": 1, "songs": songs}, f, ensure_ascii=False, separators=(",", ":"))
    by = {}
    for s in songs:
        by[s["licence"]] = by.get(s["licence"], 0) + 1
    print(f"{len(songs)} songs ({added} from OpenLP); {by}; {os.path.getsize(a.out)} bytes", file=sys.stderr)


if __name__ == "__main__":
    main()
