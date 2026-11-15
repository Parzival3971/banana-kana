#!/usr/bin/env python3
"""Generates app/data/*: kana.tsv, words.tsv, strokes.txt (KanjiVG, CC BY-SA 3.0),
and fonts (kana.ttf, ui.ttf, fallback.ttf + licences).
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
あい|ai|love
いえ|ie|house
うえ|ue|above
あお|ao|blue
あう|au|to meet
いいえ|iie|no
あか|aka|red
いけ|ike|pond
こい|koi|carp
こえ|koe|voice
きく|kiku|to listen
かく|kaku|to write
いく|iku|to go
ここ|koko|here
いか|ika|squid
くうき|kuuki|air
おおきい|ookii|big
えいご|eigo|English
かさ|kasa|umbrella
いす|isu|chair
うし|ushi|cow
あし|ashi|leg
すき|suki|to like
そこ|soko|there
せき|seki|seat
おかし|okashi|sweets
しお|shio|salt
いち|ichi|one
くち|kuchi|mouth
て|te|hand
たこ|tako|octopus
した|shita|below
そと|soto|outside
つくえ|tsukue|desk
きた|kita|north
たかい|takai|tall / expensive
ちかてつ|chikatetsu|subway
あつい|atsui|hot
なつ|natsu|summer
なに|nani|what
いぬ|inu|dog
にし|nishi|west
ひと|hito|person
ふね|fune|ship
ほし|hoshi|star
はな|hana|flower
へた|heta|bad at
アイス|aisu|ice cream
ココア|kokoa|cocoa
テスト|tesuto|test
タクシー|takushii|taxi
スイス|suisu|Switzerland
テニス|tenisu|tennis
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
        seen = set()
        for w in WORDS:
            k = w.split("|")[0]
            if k in seen: continue
            seen.add(k); f.write(w.replace("|", "\t") + "\n")

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

    hr = hint_rows(rows)
    with open(os.path.join(DATA, "hints.tsv"), "w", encoding="utf-8") as f:
        f.write("# Lesson hints, written for Banana Kana.\n")
        for g, h in hr: f.write(f"{g}\t{h}\n")
    print("hints:", len(hr))
    build_fonts(rows)


HINTS_H = {
 "あ":"A cross with a big loop swinging round it, like someone flailing: \"ahh!\"",
 "い":"Two short strokes side by side, like a pair of eels: \"ee\".",
 "う":"A dash over a hook, like a face pulling an \"oo\" pout.",
 "え":"A dash over a zigzag that ends in a kick: an energetic \"eh!\"",
 "お":"Like あ, but the loop opens out to the right with a dot: \"oh!\" with a raised hand.",
 "か":"A strong stroke with a tick beside it, like a karate chop: \"ka!\"",
 "き":"Two bars over a hook: a key with its teeth sticking out.",
 "く":"One bend, like a beak opening to say \"ku\".",
 "け":"A tall line next to a crossed hook, like a keg on its stand.",
 "こ":"Two short strokes, like a pair of coins stacked: \"ko\".",
 "さ":"One bar and a curve: like き with one bar missing. Say \"sa\".",
 "し":"A single hook, like a fishing line: \"shi\" (she) caught a fish.",
 "す":"A bar with a loop dropping through it, like a swing: \"su\".",
 "せ":"A bar crossed by two posts, one bending down: like a seat seen side-on.",
 "そ":"A zigzag that runs into a curve, like a sewing thread: \"so\".",
 "た":"A t on the left with こ beside it: \"ta\".",
 "ち":"A bar with a curve under it, like a 5 drawn backwards: \"chi\" (cheek).",
 "つ":"One wave, like a tsunami rolling in: \"tsu\".",
 "て":"A bar that drops into a curve, like an arm and hand: \"te\" means hand.",
 "と":"A short tick above a curve, like a toe with a splinter: \"to\".",
 "な":"A cross, a dot and a loop: a knot tied at the end: \"na\".",
 "に":"A tall line next to こ, like a knee beside two steps: \"ni\".",
 "ぬ":"Like め, but with a little loop at the end: noodles twirled on chopsticks: \"nu\".",
 "ね":"A post with a curl ending in a loop, like a cat's tail: \"ne\" (neko).",
 "の":"One swirl, like a \"no\" sign drawn in one go.",
 "は":"Like け, with a loop at the bottom: \"ha\" - a laugh with a curl.",
 "ひ":"A wide smile: \"hee hee\".",
 "ふ":"Several small marks spread out, like Mt Fuji with clouds: \"fu\".",
 "へ":"A single peak, like a roof or a hill: \"he\".",
 "ほ":"Like は with an extra bar on top: a hoe leaning on a post.",
 "ま":"Two bars crossed by a post that ends in a loop: \"ma\" carrying a bag.",
 "み":"A curl with a stroke through its tail, like the number 21: \"mi\".",
 "む":"A bar, a loop and a dot: a cow going \"moo\".",
 "め":"Two crossing strokes curling round, like an eye: \"me\".",
 "も":"A fish hook with two bars across it: catch \"more\" fish.",
 "や":"A curve, a dot and a long stroke, like a yacht's sail: \"ya\".",
 "ゆ":"A loop with a line straight through it, like a fish: \"yu\".",
 "よ":"A short bar on a post with a loop at the foot: a yo-yo hanging down.",
 "ら":"A dot over a curl, like a rabbit's ear and body: \"ra\".",
 "り":"Two strokes, the right one longer, like reeds by a river: \"ri\".",
 "る":"Like ろ, but ending in a loop: a route that goes round again: \"ru\".",
 "れ":"A post with a curling stroke that kicks out at the end: \"re\".",
 "ろ":"A zigzag with an open curve, like a road bending: \"ro\".",
 "わ":"A post and a big open curve, like a wave: \"wa\".",
 "を":"A bar, a zigzag and a hook: someone shouting \"whoa!\" Read as \"o\" in sentences.",
 "ん":"A single stroke like a lower-case n with a tail: \"n\".",
}
HINTS_K = {
 "ア":"Like an axe head on a handle: \"a\".",
 "イ":"A slash and a post, like an easel: \"i\".",
 "ウ":"A small tick over a box shape: like う, a pouting \"oo\".",
 "エ":"Like a capital I lying on its girders: an \"e\"-beam.",
 "オ":"A cross with a slash, like a person in a big coat: \"o\".",
 "カ":"Like か without its tick: \"ka\".",
 "キ":"Two bars and a post, like a key: \"ki\".",
 "ク":"Two strokes like the edge of a cookie with a bite out: \"ku\".",
 "ケ":"Like the letter K leaning: \"ke\".",
 "コ":"Two corners of a box: \"ko\".",
 "サ":"A bar with two posts, one hooked: \"sa\".",
 "シ":"Two dots and an upward stroke, like a smiling face looking right: \"shi\". Compare ツ.",
 "ス":"A bar with a leg kicking out, like someone skiing: \"su\".",
 "セ":"Like せ with straighter lines: \"se\".",
 "ソ":"One dot and one long stroke falling down: \"so\". Compare ン.",
 "タ":"Like ク with an extra line inside: \"ta\".",
 "チ":"A short tick, a bar and a long stroke: \"chi\".",
 "ツ":"Three strokes falling downwards, like a wave coming in: \"tsu\". Compare シ.",
 "テ":"Two bars and a hanging stroke, like a telegraph pole: \"te\".",
 "ト":"A post with a tick, like a toe: \"to\".",
 "ナ":"A cross with a slanted leg: \"na\".",
 "ニ":"Two bars, like に without its post: \"ni\".",
 "ヌ":"A hooked stroke crossed by a short slash: \"nu\".",
 "ネ":"A dot on a post with arms, like a necktie: \"ne\".",
 "ノ":"One stroke: like a \"no\" slash.",
 "ハ":"Two strokes spreading apart, like a laugh: \"ha\".",
 "ヒ":"A bar and a hooked stroke, like a heel: \"hi\".",
 "フ":"A bar that bends down, like ふ simplified: \"fu\".",
 "ヘ":"The same peak as へ: \"he\".",
 "ホ":"A cross with two little legs, like a holy cross: \"ho\".",
 "マ":"A bar that folds back with a tick, like a mask: \"ma\".",
 "ミ":"Three short strokes stacked: \"mi\".",
 "ム":"A triangle with an arm out, like a cow's head: \"mu\".",
 "メ":"Two crossing strokes, like an X marked on a map: \"me\".",
 "モ":"Two bars with a hooked post, like も straightened: \"mo\".",
 "ヤ":"Like や straightened out: \"ya\".",
 "ユ":"A bar with a hooked corner, like a U-bend: \"yu\".",
 "ヨ":"Three bars joined on the right, like a backwards E: \"yo\".",
 "ラ":"A short bar over a hooked stroke: \"ra\".",
 "リ":"Two strokes, the right one longer: \"ri\", like り.",
 "ル":"Two strokes, the right one with a tail: like roots: \"ru\".",
 "レ":"One stroke that kicks up, like a tick: \"re\".",
 "ロ":"A square box: \"ro\". Like ろ squared off.",
 "ワ":"A box with an open bottom, like a wide mouth: \"wa\".",
 "ヲ":"Like フ with an extra bar: \"wo\". Rare in modern Japanese.",
 "ン":"One dot and one stroke rising up: \"n\". Compare ソ.",
}
def hint_rows(rows):
    def base_of(g, script):  # voiced -> plain base
        voiced = "がぎぐげござじずぜぞだぢづでどばびぶべぼ"; plain = "かきくけこさしすせそたちつてとはひふへほ"
        semi = "ぱぴぷぺぽ"
        h = g if script == "hiragana" else "".join(chr(ord(c)-0x60) if "ァ" <= c <= "ヶ" else c for c in g)
        conv = (lambda x: x) if script == "hiragana" else (lambda x: "".join(chr(ord(c)+0x60) if "ぁ" <= c <= "ゖ" else c for c in x))
        if h in voiced: return conv(plain[voiced.index(h)]), "dakuten"
        if h in semi: return conv("はひふへほ"[semi.index(h)]), "handakuten"
        return None, None
    out = []
    for script, g, r, grp in rows:
        table = HINTS_H if script == "hiragana" else HINTS_K
        if g in table: out.append((g, table[g])); continue
        if grp == "yoon":
            out.append((g, f'{g[0]} plus a small {g[1]}, said as one beat: "{r}".'))
            continue
        b, kind = base_of(g, script)
        if kind == "dakuten":
            out.append((g, f'{b} with two little marks (dakuten): the sound turns voiced, "{r}".'))
        elif kind == "handakuten":
            out.append((g, f'{b} with a little circle (handakuten): h turns into p, "{r}".'))
    return out

GF = "https://raw.githubusercontent.com/google/fonts/main/"
def fetch(url, name):
    os.makedirs("/tmp/gf", exist_ok=True); path = os.path.join("/tmp/gf", name)
    if not os.path.exists(path): urllib.request.urlretrieve(url, path)
    return path

def build_fonts(rows):
    """kana.ttf  : Zen Maru Gothic Bold, subset to kana + Japanese punctuation (OFL)
       ui.ttf    : Pixelify Sans at weight 500, Latin (OFL; Banana Player's UI font)
       fallback.ttf: Noto Sans CJK JP subset, for any glyph the other two lack (OFL)"""
    lic = os.path.join(DATA, "licenses"); os.makedirs(lic, exist_ok=True)
    kana = sorted({c for _, g, _, _ in rows for c in g} | {c for w in WORDS for c in w.split("|")[0]})
    jp_extra = "ー・…。、「」？！〜（）"
    zen = fetch(GF + "ofl/zenmarugothic/ZenMaruGothic-Bold.ttf", "ZenMaruGothic-Bold.ttf")
    subprocess.run([sys.executable, "-m", "fontTools.subset", zen, f"--text={''.join(kana)}{jp_extra}",
                    "--layout-features=*", f"--output-file={os.path.join(DATA, 'kana.ttf')}"], check=True)
    pix = fetch(GF + "ofl/pixelifysans/PixelifySans%5Bwght%5D.ttf", "PixelifySans-VF.ttf")
    inst = "/tmp/gf/PixelifySans-500.ttf"
    subprocess.run([sys.executable, "-m", "fontTools.varLib.instancer", pix, "wght=500", "-o", inst], check=True)
    latin = "".join(chr(c) for c in range(0x20, 0x7f)) + "".join(chr(c) for c in range(0xa0, 0x180)) + "‹›–—‘’“”…•·×←→↑↓"
    subprocess.run([sys.executable, "-m", "fontTools.subset", inst, f"--text={latin}", "--layout-features=*",
                    f"--output-file={os.path.join(DATA, 'ui.ttf')}"], check=True)
    noto = "/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"
    fb = "".join(kana) + jp_extra + latin + "✓✗"
    subprocess.run([sys.executable, "-m", "fontTools.subset", noto, "--font-number=0", f"--text={fb}",
                    "--layout-features=*", f"--output-file={os.path.join(DATA, 'fallback.ttf')}"], check=True)
    old = os.path.join(DATA, "font.ttf")
    if os.path.exists(old): os.remove(old)
    for url, name in [(GF + "ofl/zenmarugothic/OFL.txt", "ZenMaruGothic-OFL.txt"),
                      (GF + "ofl/pixelifysans/OFL.txt", "PixelifySans-OFL.txt")]:
        open(os.path.join(lic, name), "w").write(open(fetch(url, name)).read())
    open(os.path.join(lic, "NotoSansCJK.txt"), "w").write(
        "fallback.ttf is a subset of Noto Sans CJK JP (c) Google / Adobe, SIL Open Font License 1.1.\n")
    print("fonts: kana.ttf ui.ttf fallback.ttf")

if __name__ == "__main__": main()
