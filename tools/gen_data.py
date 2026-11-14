#!/usr/bin/env python3
"""Generates app/data/*: kana.tsv, words.tsv, strokes.txt (KanjiVG, CC BY-SA 3.0),
and a subset font.
Run from repo root:  python3 tools/gen_data.py"""
import os, re, subprocess, sys, urllib.request, concurrent.futures as cf, math

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
DATA = os.path.join(ROOT, "app", "data")
os.makedirs(DATA, exist_ok=True)

# ---------------------------------------------------------------- kana tables
BASIC = [
 ("あいうえお", "a i u e o"), ("かきくけこ", "ka ki ku ke ko"),
 ("さしすせそ", "sa shi su se so"), ("たちつてと", "ta chi tsu te to"),
 ("なにぬねの", "na ni nu ne no"), ("はひふへほ", "ha hi fu he ho"),
 ("まみむめも", "ma mi mu me mo"), ("や　ゆ　よ", "ya - yu - yo"),
 ("らりるれろ", "ra ri ru re ro"), ("わ　　　を", "wa - - - wo"), ("ん", "n"),
]
VOICED = [
 ("がぎぐげご", "ga gi gu ge go"), ("ざじずぜぞ", "za ji zu ze zo"),
 ("だぢづでど", "da ji zu de do"), ("ばびぶべぼ", "ba bi bu be bo"),
 ("ぱぴぷぺぽ", "pa pi pu pe po"),
]
YOON_BASE = [("き","ki","ky"),("し","shi","sh"),("ち","chi","ch"),("に","ni","ny"),
             ("ひ","hi","hy"),("み","mi","my"),("り","ri","ry"),("ぎ","gi","gy"),
             ("じ","ji","j"),("び","bi","by"),("ぴ","pi","py")]
YOON_SMALL = [("ゃ","a"),("ゅ","u"),("ょ","o")]

def to_kata(s):
    return "".join(chr(ord(c)+0x60) if "ぁ" <= c <= "ゖ" else c for c in s)

def kana_rows():
    rows = []  # (script, glyph, romaji, group)
    def add_group(group, table):
        for chars, rom in table:
            rs = rom.split()
            for i, ch in enumerate(chars):
                if ch == "　" or rs[i] == "-": continue
                rows.append((ch, rs[i], group))
    add_group("gojuon", BASIC); add_group("dakuten", VOICED)
    for base, rom, pre in YOON_BASE:
        for sm, v in YOON_SMALL:
            r = (pre + v) if pre in ("sh","ch","j") else (pre + v)
            rows.append((base + sm, r, "yoon"))
    out = []
    for g, r, grp in rows: out.append(("hiragana", g, r, grp))
    for g, r, grp in rows: out.append(("katakana", to_kata(g), r, grp))
    return out

# ---------------------------------------------------------------- words
WORDS = """
あめ|ame|rain
いえ|ie|house
うみ|umi|sea
えき|eki|station
おかね|okane|money
かお|kao|face
くるま|kuruma|car
さくら|sakura|cherry blossom
すし|sushi|sushi
せかい|sekai|world
そら|sora|sky
つき|tsuki|moon
てがみ|tegami|letter
とり|tori|bird
なまえ|namae|name
にく|niku|meat
ねこ|neko|cat
いぬ|inu|dog
はな|hana|flower
ひと|hito|person
ふゆ|fuyu|winter
へや|heya|room
ほし|hoshi|star
まど|mado|window
みず|mizu|water
やま|yama|mountain
ゆき|yuki|snow
よる|yoru|night
りんご|ringo|apple
わたし|watashi|I / me
ほん|hon|book
あさ|asa|morning
おちゃ|ocha|tea
ごはん|gohan|rice / meal
やきとり|yakitori|grilled chicken skewers
げんき|genki|healthy / energetic
ともだち|tomodachi|friend
がっこう|gakkou|school
せんせい|sensei|teacher
でんしゃ|densha|train
じかん|jikan|time
ちず|chizu|map
ぞう|zou|elephant
ぶた|buta|pig
とけい|tokei|clock / watch
かさ|kasa|umbrella
くつ|kutsu|shoes
ふね|fune|ship
はし|hashi|chopsticks
みち|michi|road
しお|shio|salt
たまご|tamago|egg
ぎゅうにゅう|gyuunyuu|milk
きょう|kyou|today
ちゃわん|chawan|rice bowl
びょういん|byouin|hospital
りょこう|ryokou|travel
じてんしゃ|jitensha|bicycle
でんわ|denwa|telephone
コーヒー|koohii|coffee
ビール|biiru|beer
パン|pan|bread
ホテル|hoteru|hotel
メモ|memo|memo
テレビ|terebi|television
ラーメン|raamen|ramen
ケーキ|keeki|cake
アイス|aisu|ice cream
タクシー|takushii|taxi
バス|basu|bus
カメラ|kamera|camera
ゲーム|geemu|game
アニメ|anime|anime
ピザ|piza|pizza
トイレ|toire|toilet
ナイフ|naifu|knife
ドア|doa|door
ペン|pen|pen
ノート|nooto|notebook
ミルク|miruku|milk
サラダ|sarada|salad
スープ|suupu|soup
レストラン|resutoran|restaurant
テニス|tenisu|tennis
サッカー|sakkaa|soccer
ギター|gitaa|guitar
ピアノ|piano|piano
""".strip().splitlines()

# ---------------------------------------------------------------- KanjiVG strokes
def parse_path(d):
    toks = re.findall(r"[MmCcSsLlHhVvZz]|-?\d*\.?\d+(?:e-?\d+)?", d)
    i = 0; cmd = None; x = y = 0.0; sx = sy = 0.0; pts = []; lastc = None
    def num():
        nonlocal i; v = float(toks[i]); i += 1; return v
    def cubic(p0, p1, p2, p3):
        for k in range(1, 13):
            t = k / 12.0; u = 1 - t
            pts.append((u**3*p0[0]+3*u*u*t*p1[0]+3*u*t*t*p2[0]+t**3*p3[0],
                        u**3*p0[1]+3*u*u*t*p1[1]+3*u*t*t*p2[1]+t**3*p3[1]))
    while i < len(toks):
        if re.match(r"[A-Za-z]", toks[i]): cmd = toks[i]; i += 1
        if cmd in "Mm":
            nx, ny = num(), num()
            x, y = (x+nx, y+ny) if cmd == "m" else (nx, ny)
            pts.append((x, y)); sx, sy = x, y; lastc = None; cmd = "l" if cmd == "m" else "L"
        elif cmd in "Cc":
            v = [num() for _ in range(6)]
            if cmd == "c": v = [v[0]+x, v[1]+y, v[2]+x, v[3]+y, v[4]+x, v[5]+y]
            cubic((x, y), (v[0], v[1]), (v[2], v[3]), (v[4], v[5])); lastc = (v[2], v[3]); x, y = v[4], v[5]
        elif cmd in "Ss":
            v = [num() for _ in range(4)]
            if cmd == "s": v = [v[0]+x, v[1]+y, v[2]+x, v[3]+y]
            c1 = (2*x-lastc[0], 2*y-lastc[1]) if lastc else (x, y)
            cubic((x, y), c1, (v[0], v[1]), (v[2], v[3])); lastc = (v[0], v[1]); x, y = v[2], v[3]
        elif cmd in "Ll":
            nx, ny = num(), num(); x, y = (x+nx, y+ny) if cmd == "l" else (nx, ny); pts.append((x, y)); lastc = None
        elif cmd in "Hh":
            nx = num(); x = x+nx if cmd == "h" else nx; pts.append((x, y)); lastc = None
        elif cmd in "Vv":
            ny = num(); y = y+ny if cmd == "v" else ny; pts.append((x, y)); lastc = None
        else: i += 1
    return pts

def fetch_svg(cp):
    url = f"https://raw.githubusercontent.com/KanjiVG/kanjivg/master/kanji/{cp:05x}.svg"
    cache = os.path.join("/tmp/kvg", f"{cp:05x}.svg"); os.makedirs("/tmp/kvg", exist_ok=True)
    if not os.path.exists(cache):
        urllib.request.urlretrieve(url, cache)
    return open(cache, encoding="utf-8").read()

def main():
    rows = kana_rows()
    with open(os.path.join(DATA, "kana.tsv"), "w", encoding="utf-8") as f:
        for script, g, r, grp in rows: f.write(f"{script}\t{g}\t{r}\t{grp}\n")
    with open(os.path.join(DATA, "words.tsv"), "w", encoding="utf-8") as f:
        for w in WORDS: f.write(w.replace("|", "\t") + "\n")

    cps = sorted({ord(c) for _, g, _, _ in rows for c in g})
    def work(cp):
        try:
            svg = fetch_svg(cp)
            paths = re.findall(r'<path[^>]*\sd="([^"]+)"', svg)
            return cp, [parse_path(p) for p in paths]
        except Exception as e:
            return cp, None
    with cf.ThreadPoolExecutor(8) as ex: res = list(ex.map(work, cps))
    miss = [hex(c) for c, s in res if not s]
    with open(os.path.join(DATA, "strokes.txt"), "w") as f:
        f.write("# KanjiVG (c) Ulrich Apel, CC BY-SA 3.0, http://kanjivg.tagaini.net\n")
        for cp, strokes in res:
            if not strokes: continue
            f.write(f"U {cp:x} {len(strokes)}\n")
            for s in strokes: f.write(" ".join(f"{px:.1f},{py:.1f}" for px, py in s) + "\n")
    print("strokes: ok", len(res) - len(miss), "missing", miss)

    # subset font (kana + ASCII + a few symbols) from system Noto Sans CJK JP
    chars = "".join(sorted({c for _, g, _, _ in rows for c in g} | {c for w in WORDS for c in w.split("|")[0]}))
    chars += "".join(chr(c) for c in range(0x20, 0x7f)) + "ー・…。、？！→←✓✗"
    src = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    subprocess.run([sys.executable, "-m", "fontTools.subset", src, "--font-number=0",
                    f"--text={chars}", "--layout-features=*",
                    f"--output-file={os.path.join(DATA, 'font.ttf')}"], check=True)

if __name__ == "__main__": main()
