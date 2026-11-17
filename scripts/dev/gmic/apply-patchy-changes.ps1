# Applies Patchy's changes to the vendored G'MIC 4.0.5 sources. Byte-transparent
# (Latin-1 in and out, LF line ends). Every inserted block carries a PATCHY marker.
param([Parameter(Mandatory)][string]$Dir)
$enc = [Text.Encoding]::GetEncoding(28591)
function Load($name) { ,([IO.File]::ReadAllText((Join-Path $Dir $name), $enc) -split "`n") }
function Save($name, $lines) { [IO.File]::WriteAllText((Join-Path $Dir $name), ($lines -join "`n"), $enc) }
function Expect($lines, $number, $text) {
  if ($lines[$number - 1].Trim() -ne $text.Trim()) { throw "line $number is '$($lines[$number - 1])', expected '$text'" }
}
function InsertAfter($lines, $number, $new) { $lines[0..($number - 1)] + $new + $lines[$number..($lines.Count - 1)] }

# --- gmic.cpp: patch-based inpainting (edits applied bottom-up) ---
$g = Load 'gmic.cpp'
if ($g -match 'PATCHY') { throw 'gmic.cpp is already patched' }
Expect $g 948 'const bool is_blend_outer=false) {'
Expect $g 1348 'return *this;'
Expect $g 1349 '}'
$g = InsertAfter $g 1348 @('#endif // PATCHY: patchy_gmic_no_patch_algorithms')
$g = InsertAfter $g 948 @(
  '#ifdef patchy_gmic_no_patch_algorithms',
  '  // PATCHY: patch-based inpainting is compiled out of Patchy builds (docs/gmic.md).',
  '  throw CImgInstanceException(_cimg_instance',
  '                              "inpaint_patch(): Not available in this build.",',
  '                              cimg_instance);',
  '#else')
Save 'gmic.cpp' $g

# --- CImg.h (bottom-up) ---
$c = Load 'CImg.h'
if ($c -match 'PATCHY') { throw 'CImg.h is already patched' }
# A formula's run() outside critical() is never evaluated by several threads: each thread's
# interpreter copy registers under the same image list, so a run() could find another thread's
# copy and use it after that thread deleted it (docs/gmic.md, "Patchy's changes"). In-place
# edits, applied first, so the line numbers below stay the original file's. Each text is its
# lines joined with LF (this script is stored with CRLF line ends).
$lf = [string][char]10
foreach ($pair in @(
    @((('          cimg_openmp_if(*expression==''*'' || *expression=='':'' || (mp.is_parallelizable &&', '                                                                  (M2>=2 || M1>=4096) && M1*M2>=32))') -join $lf),
      (('          cimg_openmp_if(!mp.is_noncritical_run && (*expression==''*'' || *expression=='':'' || (mp.is_parallelizable &&', '                                                                  (M2>=2 || M1>=4096) && M1*M2>=32))) // PATCHY: no parallel run()') -join $lf)),
    @('      cimg_pragma_openmp(parallel if (num_threads>0 && res._height>=512) num_threads(num_threads)) {',
      '      cimg_pragma_openmp(parallel if (num_threads>0 && res._height>=512 && !mp.is_noncritical_run) num_threads(num_threads)) { // PATCHY: no parallel run()'))) {
  $joined = $c -join $lf
  $at = $joined.IndexOf($pair[0])
  if ($at -lt 0 -or $joined.IndexOf($pair[0], $at + 1) -ge 0) { throw "not exactly one match for: $($pair[0])" }
  $c = $joined.Replace($pair[0], $pair[1]) -split $lf
}
# External programs: the four popen call sites go through a helper that never starts one.
foreach ($n in 58534, 58480, 58352, 58207) {
  if ($c[$n - 1] -notmatch 'popen\(command,"r"\)') { throw "no popen at line $n" }
  $c[$n - 1] = $c[$n - 1].Replace('popen(command,"r")', 'cimg::patchy_popen(command) /* PATCHY */')
}
# PatchMatch correspondence search.
Expect $c 44736 'CImg<t2> &matching_score) const {'
Expect $c 45175 'return a_map;'
Expect $c 45176 '}'
$c = InsertAfter $c 45175 @('#endif // PATCHY: patchy_gmic_no_patch_algorithms')
$c = InsertAfter $c 44736 @(
  '#ifdef patchy_gmic_no_patch_algorithms',
  '      // PATCHY: the patch correspondence search is compiled out of Patchy builds (docs/gmic.md).',
  '      throw CImgInstanceException(_cimg_instance',
  '                                  "matchpatch(): Not available in this build.",',
  '                                  cimg_instance);',
  '#else')
Expect $c 4031 '}'
$c = InsertAfter $c 4031 @(
  '',
  '    // PATCHY: stands in for popen() at the external-loader call sites. With',
  '    // cimg_no_system_calls no program is started and the caller sees a failed open.',
  '    inline std::FILE *patchy_popen(const char *const command) {',
  '#if defined(cimg_no_system_calls) || cimg_OS!=1',
  '      cimg::unused(command);',
  '      return 0;',
  '#else',
  '      return popen(command,"r");',
  '#endif',
  '    }')
# MSVC: cimg::_snprintf calls _vsnprintf before cimg::_vsnprintf is declared, so the call binds
# to the CRT's, which does not terminate a cut string.
Expect $c 2480 '#if cimg_OS==2 && defined(_MSC_VER)'
Expect $c 2481 'inline int _snprintf(char *const s, const size_t size, const char *const format, ...) {'
$c = InsertAfter $c 2480 @(
  '    // PATCHY: declared ahead of _snprintf, whose call would otherwise bind to the CRT''s _vsnprintf,',
  '    // which leaves a cut string without its terminating zero (an unterminated G''MIC title crashed on ARM64).',
  '    inline int _vsnprintf(char *const s, const size_t size, const char *const format, va_list ap);')
Save 'CImg.h' $c
'patched'
