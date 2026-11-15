# Text alignment and justify modes

Split from [text-tool.md](text-tool.md): Photoshop's justify modes, how a paragraph's alignment is
stored, and what the options bar, Type > Alignment and the Paragraph panel show and set. The
controls themselves are in [text-tool-panels.md](text-tool-panels.md); right-to-left layout is in
[text-render-calibration-vertical-rtl.md](text-render-calibration-vertical-rtl.md).

## Justify modes

Photoshop's four justify modes (GitHub issue 672) differ only in a paragraph's last line: Justify
(last line left), Justify (last line centered), Justify (last line right) and Justify all, which
spreads the last line too. They are the Paragraph panel's and Type > Alignment's last four
entries and the script API's `justify`, `justifyCenter`, `justifyRight`, `justifyAll`.

- **Names and values are persisted.** `patchy.text.paragraph_runs` column 2 stores the name
  (`paragraph_alignment_name` / `set_paragraph_alignment`, text_document.hpp); the TySh and the
  Txt2 paragraph sheet store EngineData `/Justification` 3, 5, 4, 6 ([txt2.md](txt2.md)). A
  justified block is `Qt::AlignJustify` plus `kTextBlockJustifyLastLineProperty`; alignment edits
  merge both (`apply_text_alignment_to_editor`), never `QTextEdit::setAlignment`.
- **A block format is merged over the block's own positions only.** `mergeBlockFormat` reaches
  every block the selection touches, and the position after a paragraph break is the next
  block: `apply_paragraph_runs_to_document`, `copy_text_document_formats` and
  `scale_document_font_sizes` select up to the break. One position further gave the next
  paragraph whatever its own format leaves unset (the kept Photoshop settings, a composer, an
  explicit direction; in the commit's copy every property of the paragraph before it, so an
  alignment set at a bare caret also reached the paragraph after the caret's). Test:
  `ui_text_paragraph_runs_stay_on_their_own_paragraphs`.
- **Box and area text only** (PS 2026 probes: Arial 20 px in a 300 px box). A lone line is the
  last line, so Justify last centered and right place it like Center and Right, and Justify all
  spreads even a single word across the box letter by letter. Point text sets every justify mode
  flush left at its anchor, so neither the shift nor the spread applies there. A line on a path
  is drawn like a one-line box ([path-text.md](path-text.md#layout-and-drawing)), but neither it
  nor point text is offered a justify mode: the controls show the plain alignment and Justify is
  off (below).
- **The controls show sides; the stored name is logical** (GitHub issues 683 and 717). A stored
  name counts from the paragraph's start edge, as Qt and Photoshop keep it: `left` in a
  right-to-left paragraph stands on the right, and `justifyRight` puts its last line on the
  left. The options bar, Type > Alignment and the Paragraph panel go through
  `shown_paragraph_alignment_name` and `stored_paragraph_alignment_name` (text_document.hpp):
  left and right, and `justify` and `justifyRight`, swap for a right-to-left paragraph
  (`resolved_block_direction`, so an auto paragraph counts by its first strong character), and
  a justify mode on point or path text (`TextAlignmentFlow`) shows as, and is stored as, its
  plain alignment. `apply_shown_text_alignment_to_editor` stores a pick per paragraph, so one
  press puts left-to-right and right-to-left paragraphs on the same side; a session shows the
  caret's paragraph, a layer its first (`current_text_alignment_name`). The Justify button and
  the panel's four justify entries are disabled for point and path text
  (`current_text_justify_available`). Vertical type is not mapped. The script API (`align`,
  `textAlign`) and `apply_text_alignment_to_editor` keep the stored names. Tests:
  `ui_rtl_alignment_controls_show_and_set_sides`,
  `ui_text_path_justify_modes_draw_like_photoshop_and_show_plain_alignment`.
- **Placement**: Qt leaves the last line flush left; `justified_last_line_shift` moves it by half
  or all of its slack in every box and area line plan (`photoshop_text_layout_plan`,
  `boxed_text_line_render_items`, `area_text_layout_plan`, the caret geometry fallback).
  Justify all is QTextLayout's forced justification (`apply_justify_all_to_document`, set by
  `build_text_render_document` on box and area documents); the flag cannot be cleared, so it is
  only ever set on a freshly built document.
- Vertical text treats every justify mode as Justify (last line left).
- Patchy breaks lines one at a time, like Adobe Single-line Composer. A Photoshop paragraph set
  to Adobe Every-line Composer keeps that setting through an edit and a save
  ([txt2-kept-settings.md](txt2-kept-settings.md#line-composer)), but Patchy's own line breaks for it can differ from
  Photoshop's.
- **Right-to-left and cursive paragraphs are justified by Patchy, over their word spaces**
  (`apply_word_space_justification_to_document`, box documents, GitHub issue 683). Qt's own
  justification is wrong for both: a right-to-left line's slack is cut by its trailing space
  (`QTextEngine::justify` subtracts the leading space width), so the line ended one space short
  of the frame's right edge, and a cursive script (Arabic, Syriac, N'Ko, Mongolian, ...) gets its
  slack between letters before its spaces, or all of it in the one space after a Latin word, so
  Justify All pulled an Arabic last line apart letter by letter. For a justified block that is
  right to left or holds a cursive script, every justified line (the last one too under Justify
  All) gets `slack / gaps` as `fontWordSpacing` over the line (one format range per line: a
  format on each space alone split the line into a run per word and made a re-render of a long
  paragraph take seconds), in whole 1/64 px rounded down and less the
  last glyph's overhang, so the relayout breaks the same lines; a block whose lines move anyway
  goes back to Qt's justification. Qt then finds nothing
  left to spread. `apply_justify_all_to_document` never forces a cursive block. Photoshop
  justifies both by word spaces (and kashidas, which Patchy does not insert). Area text is not
  covered: it places its own lines, so a cursive Justify All last line there stays unspread.
- Tests: `ui_text_justify_last_line_modes_match_photoshop_if_available` (pixel extents against
  the PS captures), `ui_text_paragraph_alignment_names_round_trip`,
  `text_engine_justify_modes_round_trip`, `psd_issue_672_justify_modes_read_if_available`
  (`local-test-fixtures/psd/issue-672-justify-modes.psd`, the reporter's file),
  `ui_rtl_justified_text_fills_the_box_to_both_edges`,
  `ui_arabic_justify_all_spreads_the_last_line_over_word_spaces`,
  `ui_issue_683_hebrew_psd_justified_lines_reach_both_edges_if_available`.
