#!/usr/bin/env python3
"""Source lint for the test suites (docs/testing.md, "Test source lint").

Errors (exit 1):
  - unguarded-timer: a QTimer::singleShot lambda whose body holds a CHECK( outside a try
    block, a when_dialog_visible(...) call and a nested guarded(...) or singleShot call. A
    failing CHECK there throws across Qt's event dispatch (macOS aborts in the CFRunLoop
    frames). Wrap the lambda: QTimer::singleShot(N, guarded([&] { ... })), or drive a modal
    dialog with when_dialog_visible.
  - wall-clock: a CHECK that compares a measured time (QElapsedTimer::elapsed(), a variable
    taken from a clock, a *_ms or elapsed* variable) with a bound under 60 s, outside the perf
    harness (tests/perf_*.cpp). A wall-clock limit is a hang guard, never a performance bound:
    assert the structural property and keep minutes as the backstop. The one sub-minute form
    accepted is "elapsed < <injected delay constant> / N", the check that a step did not
    wait for a worker that a test made slow on purpose.
  - A wall-clock finding listed in the allowlist below that no longer exists (remove the
    line: the list only shrinks).

Allowlist: scripts/lint-test-sources-allowlist.txt holds the sub-minute wall-clock checks
that predate the lint, one "wall-clock <path>: <CHECK text>" line each (whitespace
collapsed). Never add to it; fix the check instead.

Usage: python scripts/lint-test-sources.py [--list]   (--list prints every finding, allowed
or not). Stdlib only; run from anywhere inside the repository.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ALLOWLIST = ROOT / 'scripts' / 'lint-test-sources-allowlist.txt'
WALL_CLOCK_LIMIT_MS = 60000

# ---------------------------------------------------------------------------------------
# Brace, string and comment aware scanning. mask(text) blanks comment text and the inside
# of string and character literals (newlines kept), so offsets map 1:1 to the source and a
# brace or CHECK( inside a literal or comment cannot fool the structural scan.

_RAW_PREFIX = re.compile(r'(?:u8|u|U|L)?R"([^()\\ \t\r\n]{0,16})\(')
_STR_PREFIX = re.compile(r'(?:u8|u|U|L)?"')
_CHR_PREFIX = re.compile(r"(?:u8|u|U|L)?'")


def _skip_quoted(text, start, quote):
    j = start
    while j < len(text) and text[j] != quote:
        if text[j] == '\\':
            j += 1
        elif text[j] == '\n':
            break
        j += 1
    return j


def mask(text):
    out = list(text)
    n = len(text)

    def blank(a, b):
        for k in range(a, min(b, n)):
            if out[k] not in '\r\n':
                out[k] = ' '

    i = 0
    while i < n:
        c = text[i]
        if c == '/' and text.startswith('//', i):
            j = text.find('\n', i)
            j = n if j < 0 else j
            blank(i, j)
            i = j
            continue
        if c == '/' and text.startswith('/*', i):
            j = text.find('*/', i + 2)
            j = n if j < 0 else j + 2
            blank(i, j)
            i = j
            continue
        after_identifier = i > 0 and (text[i - 1].isalnum() or text[i - 1] == '_')
        if not after_identifier:
            m = _RAW_PREFIX.match(text, i)
            if m:
                close = ')' + m.group(1) + '"'
                j = text.find(close, m.end())
                j = n if j < 0 else j
                blank(m.end(), j)
                i = j + len(close)
                continue
            m = _STR_PREFIX.match(text, i) or _CHR_PREFIX.match(text, i)
            if m:
                quote = text[m.end() - 1]
                j = _skip_quoted(text, m.end(), quote)
                blank(m.end(), j)
                i = j + 1
                continue
        elif c == '"':
            j = _skip_quoted(text, i + 1, '"')
            blank(i + 1, j)
            i = j + 1
            continue
        # A ' after an identifier or digit is a digit separator (1'000), not a literal.
        i += 1
    return ''.join(out)


_OPEN = {'(': ')', '[': ']', '{': '}'}
_CLOSE = {')', ']', '}'}


def match_bracket(masked, start):
    """Offset of the bracket closing the one at `start`, or -1."""
    stack = []
    for k in range(start, len(masked)):
        ch = masked[k]
        if ch in _OPEN:
            stack.append(_OPEN[ch])
        elif ch in _CLOSE:
            if not stack or stack[-1] != ch:
                return -1
            stack.pop()
            if not stack:
                return k
    return -1


def split_args(masked, open_paren, close_paren):
    """[(start, end)] spans of the top-level arguments between the parentheses."""
    spans = []
    depth = 0
    begin = open_paren + 1
    for k in range(open_paren + 1, close_paren):
        ch = masked[k]
        if ch in _OPEN:
            depth += 1
        elif ch in _CLOSE:
            depth -= 1
        elif ch == ',' and depth == 0:
            spans.append((begin, k))
            begin = k + 1
    spans.append((begin, close_paren))
    return spans


def trim(masked, start, end):
    while start < end and masked[start].isspace():
        start += 1
    while end > start and masked[end - 1].isspace():
        end -= 1
    return start, end


def lambda_body(masked, start, end):
    """For a lambda expression spanning [start, end): (body_open, body_close) or None."""
    if start >= end or masked[start] != '[':
        return None
    close = match_bracket(masked, start)
    if close < 0 or close >= end:
        return None
    k = close + 1
    while k < end:
        ch = masked[k]
        if ch == '(':
            k = match_bracket(masked, k)
            if k < 0:
                return None
            k += 1
        elif ch == '{':
            body_close = match_bracket(masked, k)
            return (k, body_close) if body_close == end - 1 else None
        elif ch == '<' and masked[k:k + 2] != '<=':
            depth = 0
            while k < end:
                depth += {'<': 1, '>': -1}.get(masked[k], 0)
                if depth == 0:
                    break
                k += 1
            k += 1
        else:
            k += 1  # whitespace, mutable, noexcept, -> return type, attributes
    return None


SINGLE_SHOT = re.compile(r'\bQTimer\s*::\s*singleShot\s*\(')
CHECK_CALL = re.compile(r'\bCHECK\s*\(')
GUARD_CALL = re.compile(r'(?:\bpatchy\s*::\s*test\s*::\s*ui\s*::\s*)?\bguarded\s*\(')
WHEN_VISIBLE = re.compile(r'\bwhen_dialog_visible\s*\(')
TRY_BLOCK = re.compile(r'\btry\s*\{')


def unguarded_checks(masked, body_open, body_close):
    """CHECK( offsets in a lambda body that can throw out of it: not inside a try block, a
    when_dialog_visible / guarded call, or a nested singleShot (judged on its own)."""
    regions = []
    for pattern in (WHEN_VISIBLE, GUARD_CALL, SINGLE_SHOT, TRY_BLOCK):
        for m in pattern.finditer(masked, body_open + 1, body_close):
            bracket = m.end() - 1
            close = match_bracket(masked, bracket)
            if close > 0:
                regions.append((bracket, close))
    return [m.start() for m in CHECK_CALL.finditer(masked, body_open + 1, body_close)
            if not any(a < m.start() < b for a, b in regions)]


def single_shot_calls(masked):
    """Every QTimer::singleShot call: dicts with its span, its last argument and, for an
    inline lambda, the lambda body and the body's unguarded CHECK offsets."""
    for m in SINGLE_SHOT.finditer(masked):
        open_paren = m.end() - 1
        close_paren = match_bracket(masked, open_paren)
        if close_paren < 0:
            yield {'start': m.start(), 'kind': 'unbalanced'}
            continue
        last_start, last_end = trim(masked, *split_args(masked, open_paren, close_paren)[-1])
        call = {'start': m.start(), 'open': open_paren, 'close': close_paren,
                'arg_start': last_start, 'arg_end': last_end, 'kind': 'other'}
        if GUARD_CALL.match(masked, last_start):
            call['kind'] = 'guarded'
        elif masked.startswith('[', last_start):
            body = lambda_body(masked, last_start, last_end)
            if body is None:
                call['kind'] = 'unparsed-lambda'
            else:
                call['kind'] = 'lambda'
                call['body'] = body
                call['checks'] = unguarded_checks(masked, *body)
        yield call


def line_of(text, offset):
    return text.count('\n', 0, offset) + 1


# ---------------------------------------------------------------------------------------
# Wall-clock checks.

_CLOCK_RHS = re.compile(r'elapsed\s*\(|nsecsElapsed|steady_clock|high_resolution_clock|system_clock|'
                        r'duration_cast|duration\s*<|msecsTo|\btimed\s*\(')
_TIME_NAME = re.compile(r'(?:^|_)(?:ms|us|ns|millis|seconds|secs)$|^elapsed|_elapsed$')
_DECLARATION = re.compile(r'\b(?:const\s+)?(?:auto|qint64|double|float|int|long\s+long|long|std::int64_t|'
                          r'std::uint64_t|std::size_t)\s+(\w+)\s*(?:=|\{)\s*([^;]*);')
_CONSTANT = re.compile(r'\bconstexpr\s+(?:[\w:]+\s+)+(\w+)\s*(?:=|\{)\s*([0-9][0-9\']*(?:\.[0-9]*)?)')
_NUMBER = r'[0-9][0-9\']*(?:\.[0-9]*)?(?:[eE][+-]?[0-9]+)?[uUlLfF]*'
_ELAPSED_CALL = r'\b\w+\s*(?:\.|->)\s*(?:elapsed|nsecsElapsed)\s*\(\s*\)'


def _number(text):
    return float(re.sub(r'[uUlLfF\']', '', text))


def time_variables(masked):
    names = {}
    for m in _DECLARATION.finditer(masked):
        name, rhs = m.group(1), m.group(2)
        if re.fullmatch(r'\s*' + _NUMBER + r'\s*\}?\s*', rhs) and not _TIME_NAME.search(name):
            continue
        if _CLOCK_RHS.search(rhs) or (_TIME_NAME.search(name) and not name.startswith('k')):
            names[name] = True
    return names


def constants(masked):
    return {m.group(1): _number(m.group(2)) for m in _CONSTANT.finditer(masked)}


def _bound_ms(bound, values):
    """The bound in ms, 'accepted' for the injected-delay form, or None when unknown."""
    bound = bound.strip()
    while bound.startswith('(') and match_bracket(bound, 0) == len(bound) - 1:
        bound = bound[1:-1].strip()
    m = re.fullmatch(r'(\w+)\s*/\s*' + _NUMBER, bound)
    if m and 'delay' in m.group(1).lower():
        return 'accepted'
    m = re.fullmatch(_NUMBER, bound)
    if m:
        return _number(bound)
    if bound in values:
        return values[bound]
    m = re.fullmatch(r'(?:patchy\s*::\s*test\s*::\s*)?time_budget_ms\s*\(\s*(' + _NUMBER + r'|\w+)\s*\)', bound)
    if m:
        inner = m.group(1)
        return values.get(inner, None) if not inner[0].isdigit() else _number(inner)
    m = re.fullmatch(r'std\s*::\s*chrono\s*::\s*(milliseconds|seconds|minutes)\s*\(\s*(' + _NUMBER + r')\s*\)', bound)
    if m:
        return _number(m.group(2)) * {'milliseconds': 1, 'seconds': 1000, 'minutes': 60000}[m.group(1)]
    return None


def wall_clock_findings(text, masked):
    timers = time_variables(masked)
    values = constants(masked)
    names = '|'.join(re.escape(name) for name in sorted(timers, key=len, reverse=True))
    term = _ELAPSED_CALL + (r'|(?:\b\w+\s*\.\s*)?\b(?:' + names + r')\b' if names else '')
    term = r'(?P<term>(?:' + term + r')(?:\s*\.\s*count\s*\(\s*\))?)'
    bound = r'(?P<bound>\(?[\w:\s./()]*?\)?)'
    forward = re.compile(term + r'\s*(?P<op><=?)\s*' + bound + r'\s*$')
    findings = []
    for m in CHECK_CALL.finditer(masked):
        close = match_bracket(masked, m.end() - 1)
        if close < 0:
            continue
        expression = masked[m.end():close]
        for part in re.split(r'&&|\|\|', expression):
            part = part.strip()
            hit = forward.search(part)
            if not hit:
                continue
            seconds = re.search(r'sec|_s\b', hit.group('term')) is not None
            value = _bound_ms(hit.group('bound'), values)
            if value is None or value == 'accepted':
                continue
            if seconds:
                value *= 1000
            if value < WALL_CLOCK_LIMIT_MS:
                snippet = ' '.join(text[m.start():close + 1].split())
                findings.append((line_of(text, m.start()), snippet))
    return findings


# ---------------------------------------------------------------------------------------

def test_sources():
    tests = ROOT / 'tests'
    for path in sorted(tests.rglob('*.cpp')) + sorted(tests.rglob('*.hpp')):
        yield path


def is_perf_harness(relative):
    return re.match(r'tests/perf_[^/]*\.(cpp|hpp)$', relative) is not None


def read_allowlist():
    allowed = set()
    if ALLOWLIST.exists():
        for raw in ALLOWLIST.read_text(encoding='utf-8').splitlines():
            line = raw.strip()
            if line and not line.startswith('#'):
                allowed.add(' '.join(line.split()))
    return allowed


def main(argv):
    list_all = '--list' in argv
    allowed = read_allowlist()
    seen = set()
    errors = []
    listed = []
    for path in test_sources():
        relative = path.relative_to(ROOT).as_posix()
        text = path.read_bytes().decode('utf-8', errors='replace')
        masked = mask(text)
        for call in single_shot_calls(masked):
            if call['kind'] == 'lambda' and call['checks']:
                errors.append(f'{relative}:{line_of(text, call["start"])}: unguarded-timer: QTimer::singleShot lambda '
                              f'with CHECK( at line {line_of(text, call["checks"][0])}; wrap it in guarded(...) '
                              'or use when_dialog_visible')
        if is_perf_harness(relative):
            continue
        for line, snippet in wall_clock_findings(text, masked):
            key = f'wall-clock {relative}: {snippet}'
            listed.append(f'{relative}:{line}: {key}')
            if key in allowed:
                seen.add(key)
            else:
                errors.append(f'{relative}:{line}: wall-clock: sub-minute time bound {snippet}; assert the structural '
                              'property and keep minutes as the backstop (docs/testing.md, "Failure and lifetime traps")')
    for key in sorted(allowed - seen):
        errors.append(f'{ALLOWLIST.relative_to(ROOT).as_posix()}: stale entry, the check is gone or changed: {key}')
    if list_all:
        for entry in listed:
            print(entry)
    for error in errors:
        print(f'[FAIL] {error}')
    print(f'[SUMMARY] test source lint: {len(errors)} errors, {len(seen)} allowlisted wall-clock checks')
    return 1 if errors else 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
