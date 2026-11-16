"""Builds the benchmark pages' description: pages.json (ground truth) and make_pages.js (a
Patchy script that typesets each page with Patchy's own text engine and exports a PNG).

    python -I make_pages.py <out-dir>

Then:  patchy.exe --headless --run-script <out-dir>/make_pages.js --script-arg out=<out-dir>

Every page is A4 at 300 ppi (2480 x 3508). The text is written here: the Japanese mixes the
opening of Natsume Soseki's "Wagahai wa Neko de Aru" (1905, public domain) with sentences
written for this benchmark that carry what recognizers trip on (long vowel marks, small
kana, brackets, digits, Latin words). Lines are broken here, so the ground truth is exact.
"""
import json
import sys
from pathlib import Path

W, H = 2480, 3508

JA = (
    "吾輩は猫である。名前はまだ無い。どこで生れたかとんと見当がつかぬ。"
    "何でも薄暗いじめじめした所でニャーニャー泣いていた事だけは記憶している。"
    "吾輩はここで始めて人間というものを見た。しかもあとで聞くとそれは書生という人間中で一番獰悪な種族であったそうだ。"
    "この書生というのは時々我々を捕えて煮て食うという話である。"
    "しかしその当時は何という考もなかったから別段恐しいとも思わなかった。"
    "ただ彼の掌に載せられてスーと持ち上げられた時何だかフワフワした感じがあったばかりである。"
    "コンピューターのスクリーンショットをダウンロードして、メールで送ってください。"
    "「今日は良い天気ですね」と彼女は言った。「ええ、散歩に行きましょうか」と私は答えた。"
    "東京から大阪までは新幹線で約2時間30分、料金は13,870円です。"
    "ちょっと待って、きゃあ、びっくりした！本当にそうなの？"
    "図書館の開館時間は午前9時から午後5時までで、月曜日は休館日となっております。"
    "このソフトウェアはWindows、macOS、Linuxで動作し、レイヤー、マスク、フィルターを備えています。"
    "山路を登りながら、こう考えた。智に働けば角が立つ。情に棹させば流される。意地を通せば窮屈だ。とかくに人の世は住みにくい。"
    "住みにくさが高じると、安い所へ引き越したくなる。どこへ越しても住みにくいと悟った時、詩が生れて、画が出来る。"
)

EN = (
    "The quick brown fox jumps over the lazy dog while seventeen wizards quietly judge the box. "
    "Patchy reads the text in a scanned page and places an editable line over each printed one, "
    "so a correction is a matter of typing rather than painting. In 1984 the invoice totalled "
    "$1,250.75 (plus 8.5% tax); by 2026 the same order cost 3,499.00. Does the recognizer keep "
    "apostrophes in words like don't and it's, hyphens in state-of-the-art, and the difference "
    "between l, I and 1, or O and 0? Email support@example.org or call +1 (555) 010-2368. "
    "Chapter 7 begins on page 142: \"Nothing is so difficult as not deceiving oneself.\" "
    "A long paragraph of ordinary prose is the commonest thing an OCR engine meets, so most of "
    "this page is exactly that: plain sentences of ordinary length, with commas, full stops and "
    "the occasional question. Are the line ends kept? Is the reading order top to bottom? "
)

LATIN = (
    "Der schöne Frühling übermäßig grüßt die Straße; Fußgänger genießen Äpfel und Öl. "
    "Le garçon français a reçu un cadeau à Noël, où l'été fut très agréable près du château. "
    "El niño español comió piñas y jalapeños; ¿cuándo llegará mañana? ¡Qué sorpresa! "
    "O coração do irmão não está em São Paulo; ação, emoção e três pães. "
    "Żółć gęślą jaźń: pchnąć w tę łódź jeża lub ośm skrzyń fig. "
    "Příliš žluťoučký kůň úpěl ďábelské ódy. Árvíztűrő tükörfúrógép. "
    "Il caffè è più buono così, perché lassù c'è già la città. "
)

RU = (
    "Съешь же ещё этих мягких французских булок, да выпей чаю. "
    "В 1961 году Юрий Гагарин совершил первый полёт человека в космос, который длился 108 минут. "
    "Широкая электрификация южных губерний даст мощный толчок подъёму сельского хозяйства. "
    "Программа читает текст на отсканированной странице и ставит редактируемую строку поверх каждой напечатанной. "
    "Сколько стоит билет до Санкт-Петербурга? Примерно 3 450 рублей, если покупать заранее. "
    "Объём файла не превышает 25 МБ; поддерживаются форматы PNG, JPEG и PDF. "
)

KO = (
    "다람쥐 헌 쳇바퀴에 타고파. 키스의 고유조건은 입술끼리 만나야 하고 특별한 기술은 필요치 않다. "
    "이 프로그램은 스캔한 페이지의 글자를 읽어 인쇄된 줄마다 편집할 수 있는 텍스트를 올려놓습니다. "
    "서울에서 부산까지 KTX로 약 2시간 40분이 걸리며 요금은 59,800원입니다. "
    "도서관은 오전 9시부터 오후 6시까지 열고 월요일에는 쉽니다. "
    "오늘 날씨가 참 좋네요. 같이 산책하러 갈까요? 네, 좋아요! "
    "한글은 1443년에 세종대왕이 만들었고 1446년에 반포되었습니다. "
)


def wrap_chars(text: str, width: int) -> list[str]:
    """Fixed-width breaks for scripts without spaces. Never starts a line with closing punctuation."""
    lines, start = [], 0
    while start < len(text):
        end = min(len(text), start + width)
        while end < len(text) and text[end] in "、。」！？）,.ー":
            end += 1
        lines.append(text[start:end])
        start = end
    return lines


def wrap_words(text: str, width: int) -> list[str]:
    lines, current = [], ""
    for word in text.split():
        if current and len(current) + 1 + len(word) > width:
            lines.append(current)
            current = word
        else:
            current = (current + " " + word).strip()
    if current:
        lines.append(current)
    return lines


def repeat_to(text: str, count: int) -> str:
    out = ""
    while len(out) < count:
        out += text
    return out[:count]


def pages() -> list[dict]:
    result = []

    def add(name, language, lines, font, size, vertical=False, leading=1.0):
        result.append({"name": name, "language": language, "lines": lines, "font": font, "size": size,
                       "vertical": vertical})

    # Body text at 10.5 pt and 8 pt (44 and 33 px at 300 ppi), the sizes books are set in.
    add("en_times_44", "en", wrap_words(repeat_to(EN, 3400), 92), "Times New Roman", 44)
    add("en_arial_33", "en", wrap_words(repeat_to(EN, 5200), 118), "Arial", 33)
    add("latin_times_44", "latin", wrap_words(repeat_to(LATIN, 3000), 92), "Times New Roman", 44)
    add("ru_times_44", "ru", wrap_words(repeat_to(RU, 3000), 88), "Times New Roman", 44)
    add("ko_malgun_44", "ko", wrap_words(repeat_to(KO, 1900), 58), "Malgun Gothic", 44)
    add("ja_h_mincho_44", "ja", wrap_chars(repeat_to(JA, 1500), 42)[:36], "Yu Mincho", 44)
    add("ja_h_gothic_33", "ja", wrap_chars(repeat_to(JA, 2600), 56)[:46], "Yu Gothic", 33)
    # Vertical: each line here is one column, columns run right to left.
    add("ja_v_mincho_44", "ja", wrap_chars(repeat_to(JA, 1500), 58)[:24], "Yu Mincho", 44, vertical=True)
    add("ja_v_gothic_33", "ja", wrap_chars(repeat_to(JA, 2600), 78)[:32], "Yu Gothic", 33, vertical=True)
    add("ja_v_mincho_60", "ja", wrap_chars(repeat_to(JA, 900), 44)[:18], "Yu Mincho", 60, vertical=True)
    return result


def main() -> int:
    out = Path(sys.argv[1])
    out.mkdir(parents=True, exist_ok=True)
    described = pages()
    (out / "pages.json").write_text(json.dumps({"width": W, "height": H, "pages": described}, ensure_ascii=False,
                                               indent=1), encoding="utf-8")
    script = [
        "// Generated by make_pages.py: typesets the benchmark pages and exports them.",
        "var out = patchy.args.out;",
        "var pages = " + json.dumps(described, ensure_ascii=True) + ";",
        "for (var i = 0; i < pages.length; i++) {",
        "  var p = pages[i];",
        "  var doc = app.newDocument(%d, %d, {name: p.name});" % (W, H),
        "  var paper = doc.addLayer('Paper');",
        "  paper.fill('#ffffff');",
        "  var options = {font: p.font, size: p.size, color: '#141414'};",
        "  if (p.vertical) { options.orientation = 'vertical'; options.x = %d - 280; options.y = 260; }" % W,
        "  else { options.x = 240; options.y = 260 + p.size; }",
        "  var layer = doc.addTextLayer(p.lines.join('\\n'), options);",
        "  var b = layer.bounds;",
        "  console.log(p.name + ' text bounds ' + JSON.stringify(b));",
        "  doc.exportAs(out + '/' + p.name + '.png');",
        "}",
        "patchy.setResult({pages: pages.length});",
    ]
    (out / "make_pages.js").write_text("\n".join(script) + "\n", encoding="utf-8")
    print(len(described), "pages described")
    return 0


if __name__ == "__main__":
    sys.exit(main())
