#!/usr/bin/env python3
"""Monkey / fuzz harness for cppsekai.exe - hunt for logic holes, don't fix them.

Runs a list of cases against a **sandbox copy** of the game and reports what
broke. Three families of case, because this program has three kinds of input a
player can corrupt by hand:

  cli     - command line arguments (out of range, non-numeric, missing values,
            contradictory combinations). Every one of these reaches a numeric
            field somewhere; the question is whether it is clamped or lands raw.
  data    - files the program owns but a user can edit: userdata.json,
            profiles/*.json, the music tables, chartdl.json, .sus charts.
            Malformed here is the classic "silently resets" / "crashes on load".
  ui      - random --fake-pad button sequences. That switch synthesises real SDL
            key events, so it drives the actual input path headlessly (a posted
            WM_ message does not reach ImGui - see AGENTS.md).

Sandbox: assets + a couple of charts are copied once into a scratch directory,
and only the *mutable* files are restored between cases. Nothing outside the
scratch root is written, so the real profiles/ next to the repo are never
touched. Per-case cwd, because the log is written to the working directory and
truncated at process start.

  python .workbuddy/tools/monkey_test.py [--root <scratch dir>] [--only <substr>]
                                         [--list] [--jobs 1]

Exit code 0 always - the report is the deliverable, not the exit status.
"""
import argparse
import json
import os
import shutil
import subprocess
import sys
import time

REPO = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = os.path.join(REPO, 'build')
DEFAULT_ROOT = os.path.join(os.environ.get('TEMP', '/tmp'), 'cppsekai_monkey')

# Files the game writes and re-reads. Restored before each case.
TABLE_FILES = ['musics.json', 'music-levels.json', 'music-vocals.json', 'music-aliases.json']
# Cheap chart set: metadata + cover + BGM + one .sus per difficulty.
CHART_GLOB_PREFIX = '0001'

# High-signal log fragments. Deliberately narrow: "error" alone appears in
# perfectly normal runs, so the baseline run's matches are subtracted later.
SIGNATURES = [
    'Assertion failed', 'assertion', 'abort', 'terminate called', 'unhandled',
    'out of range', 'stoi', 'stod', 'invalid_argument', 'length_error',
    'Segmentation', 'access violation', '!!', 'nan', 'inf', 'overflow',
    'failed to', 'could not', 'cannot open', 'unable to',
]


def log(msg, progress=None):
    sys.stdout.write(msg + '\n')
    sys.stdout.flush()
    # A run takes ~12 minutes and is usually launched in the background, where
    # stdout may be sitting in a pipe that nobody reads until the end. Mirroring
    # every line to a file that is flushed is what makes "how far along is it"
    # answerable - and it is also the only record if the harness itself dies.
    if progress is not None:
        with open(progress, 'a', encoding='utf-8') as fh:
            fh.write(msg + '\n')


# ---------------------------------------------------------------------------
# Sandbox
# ---------------------------------------------------------------------------
def setup(root, force=False):
    sb = os.path.join(root, 'sb')
    pristine = os.path.join(root, 'pristine')
    fresh = force or not os.path.isdir(sb)
    if fresh:
        if os.path.isdir(root):
            shutil.rmtree(root, ignore_errors=True)
        os.makedirs(sb)
        if not os.path.isfile(os.path.join(BUILD, 'cppsekai.exe')):
            raise SystemExit('no cppsekai.exe in %s - run build.sh first' % BUILD)
        shutil.copytree(os.path.join(BUILD, 'assets'), os.path.join(sb, 'assets'))
        os.makedirs(os.path.join(sb, 'charts'))
        for name in os.listdir(os.path.join(BUILD, 'charts')):
            if name.startswith(CHART_GLOB_PREFIX):
                shutil.copy2(os.path.join(BUILD, 'charts', name), os.path.join(sb, 'charts', name))
        for name in TABLE_FILES:
            src = os.path.join(BUILD, name)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(sb, name))
        # Snapshot the read-only-by-default parts, so cases can be undone.
        os.makedirs(pristine)
        for name in TABLE_FILES:
            src = os.path.join(sb, name)
            if os.path.isfile(src):
                shutil.copy2(src, os.path.join(sb, name))
        shutil.copytree(os.path.join(sb, 'charts'), os.path.join(pristine, 'charts'))
    # The binaries are refreshed on EVERY run, not only when the sandbox is first
    # created. Reusing an existing sandbox silently tested the previous build -
    # which is exactly how a freshly fixed failure path can look like it still
    # does nothing.
    for name in ['cppsekai.exe', 'SDL2.dll', 'icon.png']:
        src = os.path.join(BUILD, name)
        if os.path.isfile(src):
            shutil.copy2(src, os.path.join(sb, name))
    return sb, pristine


def restore(sb, pristine, restore_charts):
    """Put the sandbox back to a clean state. Data files are always wiped - the
    game recreates them - so a case can never inherit the last one's mess."""
    for name in TABLE_FILES:
        src = os.path.join(pristine, name)
        if not os.path.isfile(src):
            continue
        shutil.copy2(src, os.path.join(sb, name))
    if restore_charts:
        shutil.rmtree(os.path.join(sb, 'charts'), ignore_errors=True)
        shutil.copytree(os.path.join(pristine, 'charts'), os.path.join(sb, 'charts'))
    for name in ['userdata.json', 'chartdl.json']:
        p = os.path.join(sb, name)
        if os.path.exists(p):
            os.remove(p)
    shutil.rmtree(os.path.join(sb, 'profiles'), ignore_errors=True)


def apply_mutations(sb, files):
    for rel, content in (files or {}).items():
        path = os.path.join(sb, rel.replace('/', os.sep))
        os.makedirs(os.path.dirname(path), exist_ok=True)
        mode = 'wb' if isinstance(content, bytes) else 'w'
        with open(path, mode) as fh:
            fh.write(content)


# ---------------------------------------------------------------------------
# One case
# ---------------------------------------------------------------------------
def run_case(case, root, sb, pristine, restore_charts):
    name = case['name']
    restore(sb, pristine, restore_charts=restore_charts)
    mutated = dict(case.get('files') or {})
    apply_mutations(sb, mutated)

    exe = os.path.join(sb, 'cppsekai.exe')
    args = [exe] + list(case['args'])
    shot = os.path.join(root, 'shots', name + '.png')
    os.makedirs(os.path.dirname(shot), exist_ok=True)
    if os.path.isfile(shot):
        os.remove(shot)
    timeout = case.get('timeout', 25)

    # cwd is the sandbox on purpose. CLI.md documents every resource path as
    # relative to the exe's folder, and the log is written to the CWD - pointing
    # both at the sandbox is what makes `--sus charts/0001_expert.sus` and
    # `--charts assets` resolve the way a player's command line would.
    started = time.time()
    killed = False
    try:
        proc = subprocess.Popen(args, cwd=sb, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
        try:
            out = proc.communicate(timeout=timeout)[0]
            code = proc.returncode
        except subprocess.TimeoutExpired:
            proc.kill()
            out = proc.communicate()[0]
            code = proc.returncode
            killed = True
    except OSError as exc:
        return {'name': name, 'group': case.get('group', '?'), 'args': case['args'],
                'code': None, 'hang': False, 'seconds': 0.0, 'note': 'launch failed: %s' % exc,
                'lines': [], 'shot': None, 'want_shot': False}

    text = read_log(sb, out)

    hits = []
    for line in text.splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        # The case's own name ends up inside the log line `screenshot saved:
        # <path>\cli-speed-nan.png`, so a signature like "nan" matched every case
        # whose name contained it. Skip the harness's own output paths.
        if stripped.startswith('screenshot saved'):
            continue
        low = stripped.lower()
        for sig in SIGNATURES:
            if sig.lower() in low:
                hits.append(stripped[:220])
                break

    shot_info = None
    if os.path.isfile(shot):
        shot_info = os.path.getsize(shot)
        os.remove(shot)  # the size is the datum; keep the scratch dir small

    return {
        'name': name, 'group': case.get('group', '?'), 'args': case['args'],
        'code': code, 'hang': killed, 'seconds': round(time.time() - started, 2),
        'note': 'killed after %.0fs (never exited)' % timeout if killed else '',
        'lines': hits[:12], 'shot': shot_info,
        'want_shot': '--screenshot' in case['args'],
        'mutated': list(mutated.keys()),
        'dirty_charts': any(k.startswith('charts/') or k.startswith('emptydir') for k in mutated),
    }


def read_log(sb, out):
    """Prefer cppsekai.log (--screenshot always writes it) over the captured
    pipe, and remove it so the next case starts from an empty file."""
    text = (out or b'').decode('utf-8', 'replace')
    path = os.path.join(sb, 'cppsekai.log')
    if os.path.isfile(path):
        with open(path, 'r', encoding='utf-8', errors='replace') as fh:
            file_text = fh.read()
        if len(file_text) > len(text):
            text = file_text
        os.remove(path)
    return text


# ---------------------------------------------------------------------------
# Cases
# ---------------------------------------------------------------------------
def cases():
    out = []
    add = lambda n, g, a, **kw: out.append(dict(name=n, group=g, args=a, **kw))

    base = ['--no-party', '--screenshot', 'SHOT', '--screenshot-time', '1.5']
    # SHOT is substituted per case; keep it out of the arg list by using a marker.
    def s(args, secs='1.5', shot=True):
        a = list(args)
        if shot:
            a += ['--screenshot', 'SHOT', '--screenshot-time', secs]
        return a

    # ---- baseline: a run that is known good, for signature subtraction -------
    add('baseline-songselect', 'baseline', s(['--no-party']))
    add('baseline-play', 'baseline',
        s(['--no-party', '--sus', 'charts/0001_expert.sus', '--auto', '--lead-in', '0.2'], '1.5'))

    # ---- cli: numbers out of range / not numbers ---------------------------
    for v in ['0', '-1', '-5', '999', 'abc', '', 'nan', 'inf', '1e9', '0.0000001']:
        add('cli-speed-%s' % (v or 'empty'), 'cli', s(['--no-party', '--speed', v]))
    for v in ['nan', 'inf', '-inf', '1e999', '-1e999', 'abc', '-100', '100000']:
        add('cli-offset-%s' % v.replace('e999', 'E9'), 'cli', s(['--no-party', '--offset', v]))
    for v in ['-5', 'nan', 'abc', '1e9']:
        add('cli-filler-%s' % v, 'cli', s(['--no-party', '--filler', v]))
    for v in ['-10', 'nan', 'abc', '1e9']:
        add('cli-leadin-%s' % v, 'cli', s(['--no-party', '--lead-in', v]))
    for v in ['-1', '5', 'nan', 'abc', '2']:
        add('cli-sevolume-%s' % v, 'cli', s(['--no-party', '--se-volume', v]))
    for v in ['99', '-1', 'abc', '', '2147483647']:
        add('cli-tab-%s' % (v or 'empty'), 'cli', s(['--no-party', '--settings', '--settings-tab', v]))
    for v in ['-3', '0', '99999999999999999999', 'abc', '2147483647']:
        add('cli-rank-%s' % v[:12], 'cli', s(['--no-party', '--player-rank', v]))
    for v in ['-1', '2', 'nan', 'abc', '1e9']:
        add('cli-exp-%s' % v, 'cli', s(['--no-party', '--player-exp', v]))
    for v in ['0', '1', '-100', '999999', 'abc']:
        add('cli-width-%s' % v, 'cli', s(['--no-party', '--width', v]))
    for v in ['0', '-1', 'abc', '999999']:
        add('cli-height-%s' % v, 'cli', s(['--no-party', '--height', v]))
    for v in ['-1', '0', '100000', 'abc']:
        add('cli-fps-%s' % v, 'cli', s(['--no-party', '--fps', v]))
    for v in ['0', '-1', '1e9', 'nan', 'abc']:
        add('cli-uiscale-%s' % v, 'cli', s(['--no-party', '--ui-scale', v]))
    for v in ['0x0', 'abc', '99999x1', '1x', 'x1', '-4x-4', '1x1']:
        add('cli-rendersize-%s' % v.replace('x', 'X'), 'cli', s(['--no-party', '--render-size', v]))
    for v in ['bogus', '', 'SINGLE', 'multi ']:
        add('cli-window-%s' % (v or 'empty'), 'cli', s(['--no-party', '--window', v]))
    for v in ['bogus', '', 'Multi']:
        add('cli-instance-%s' % (v or 'empty'), 'cli', s(['--no-party', '--instance', v]))
    for v in ['-5', '99999', 'abc', 'nan']:
        add('cli-resultat-%s' % v, 'cli', s(['--no-party', '--result-at', v], '1.0'))
    for v in ['-5', '99999', 'abc']:
        add('cli-restartat-%s' % v, 'cli', s(['--no-party', '--restart-at', v]))
    for v in ['99', 'abc', '-1', '']:
        add('cli-partyauto-%s' % (v or 'empty'), 'cli',
            s(['--party', '--party-auto', v], '2.0'))
    for v in ['abc', '-1']:
        add('cli-judgeframe-%s' % v, 'cli', s(['--no-party', '--judge-frame', v]))
    for v in ['-1', '99999999999']:
        add('cli-confirmflash-%s' % v, 'cli', s(['--no-party', '--confirm-flash', v], '1.0'))

    # ---- cli: missing values, unknown flags, contradictions ----------------
    add('cli-missing-value-sus', 'cli', s(['--no-party', '--sus']))
    add('cli-missing-value-speed', 'cli', s(['--no-party', '--speed']))
    add('cli-empty-sus', 'cli', s(['--no-party', '--sus', '']))
    add('cli-unknown-flag', 'cli', s(['--no-party', '--definitely-not-a-flag']))
    add('cli-bare-dash', 'cli', s(['--no-party', '-']))
    add('cli-repeated-speed', 'cli', s(['--no-party', '--speed', '1', '--speed', '12', '--offset', '1', '--offset', '-1']))
    add('cli-party-and-noparty', 'cli', s(['--party', '--no-party'], '2.0'))
    add('cli-instance-single-twice', 'cli',
        s(['--no-party', '--instance', 'single', '--instance', 'multi']))
    add('cli-all-flags-at-once', 'cli', s([
        '--no-party', '--settings', '--guess', '--profile', '--singer-panel', '--judge-sheet',
        '--result-preview', '--flick-as-tap', '--test-hits'], '2.0'))
    add('cli-settings-and-result', 'cli', s(['--no-party', '--settings', '--result-preview'], '2.0'))
    add('cli-guess-and-profile', 'cli', s(['--no-party', '--guess', '--profile'], '2.0'))

    # ---- files: missing / wrong type ---------------------------------------
    add('file-sus-missing', 'file', s(['--no-party', '--sus', 'charts/nope.sus']))
    add('file-sus-dir', 'file', s(['--no-party', '--sus', 'charts']))
    add('file-sus-is-png', 'file', s(['--no-party', '--sus', 'charts/0001.png']))
    add('file-bgm-missing', 'file',
        s(['--no-party', '--sus', 'charts/0001_expert.sus', '--bgm', 'charts/nope.mp3'], '2.0'))
    add('file-bgm-is-text', 'file',
        s(['--no-party', '--sus', 'charts/0001_expert.sus', '--bgm', 'musics.json'], '2.0'))
    add('file-cover-missing', 'file',
        s(['--no-party', '--sus', 'charts/0001_expert.sus', '--cover', 'charts/nope.png'], '2.0'))
    add('file-cover-is-mp3', 'file',
        s(['--no-party', '--sus', 'charts/0001_expert.sus', '--cover', 'charts/0001_01.mp3'], '2.0'))
    add('file-charts-missing-dir', 'file', s(['--no-party', '--charts', 'no/such/dir']))
    add('file-charts-is-file', 'file', s(['--no-party', '--charts', 'musics.json']))
    add('file-charts-empty-dir', 'file', s(['--no-party', '--charts', 'emptydir']),
        files={'emptydir/.keep': ''})
    add('file-sus-empty', 'file', s(['--no-party', '--sus', 'charts/empty.sus']),
        files={'charts/empty.sus': ''}, charts=False)
    add('file-sus-nul-bytes', 'file', s(['--no-party', '--sus', 'charts/nul.sus']),
        files={'charts/nul.sus': b'\x00' * 512}, charts=False)
    add('file-sus-random', 'file', s(['--no-party', '--sus', 'charts/rand.sus']),
        files={'charts/rand.sus': bytes(range(256)) * 8}, charts=False)
    add('file-sus-only-bom', 'file', s(['--no-party', '--sus', 'charts/bom.sus']),
        files={'charts/bom.sus': b'\xef\xbb\xbf'}, charts=False)
    add('file-sus-truncated', 'file', s(['--no-party', '--sus', 'charts/trunc.sus']),
        files={'charts/trunc.sus': '#TITLE x\n#BPM01: 120\n#00002: 01\n'}, charts=False)
    add('file-sus-huge-tick', 'file', s(['--no-party', '--sus', 'charts/huge.sus', '--auto'], '2.0'),
        files={'charts/huge.sus': '#TITLE x\n#BPM01: 120\n#00002: 01\n'
                                  '#00099: 0000000000000000000000000000000\n'}, charts=False)
    add('file-sus-bpm-zero', 'file', s(['--no-party', '--sus', 'charts/bpm0.sus', '--auto'], '2.0'),
        files={'charts/bpm0.sus': '#TITLE x\n#BPM01: 000\n#00002: 01\n'}, charts=False)
    add('file-sus-bpm-negative', 'file', s(['--no-party', '--sus', 'charts/bpmn.sus', '--auto'], '2.0'),
        files={'charts/bpmn.sus': '#TITLE x\n#BPM01: -120\n#00002: 01\n'}, charts=False)
    add('file-sus-bpm-huge', 'file', s(['--no-party', '--sus', 'charts/bpmh.sus', '--auto'], '2.0'),
        files={'charts/bpmh.sus': '#TITLE x\n#BPM01: 99999999999\n#00002: 01\n'}, charts=False)
    add('file-sus-bpm-text', 'file', s(['--no-party', '--sus', 'charts/bpmt.sus']),
        files={'charts/bpmt.sus': '#TITLE x\n#BPM01: abc\n#00002: 01\n'}, charts=False)
    add('file-sus-crlf', 'file', s(['--no-party', '--sus', 'charts/crlf.sus', '--auto'], '2.0'),
        files={'charts/crlf.sus': '#TITLE x\r\n#BPM01: 120\r\n#00002: 01\r\n'}, charts=False)
    add('file-sus-long-line', 'file', s(['--no-party', '--sus', 'charts/long.sus']),
        files={'charts/long.sus': '#TITLE ' + 'A' * 20000 + '\n'}, charts=False)
    add('file-sus-dir-named-sus', 'file', s(['--no-party', '--sus', 'charts/dir.sus']),
        files={'charts/dir.sus/keep': ''})

    # ---- data: corrupt files the program owns ------------------------------
    d = lambda n, a, **kw: out.append(dict(name=n, group='data', args=s(a), **kw))
    d('data-userdata-empty', ['--no-party'], files={'userdata.json': ''})
    d('data-userdata-garbage', ['--no-party'], files={'userdata.json': 'not json at all {{{'})
    d('data-userdata-null', ['--no-party'], files={'userdata.json': 'null'})
    d('data-userdata-array', ['--no-party'], files={'userdata.json': '[]'})
    d('data-userdata-object', ['--no-party'], files={'userdata.json': '{}'})
    d('data-userdata-wrong-types', ['--no-party'],
      files={'userdata.json': '{"settings": 5, "scores": "x", "account": []}'})
    d('data-userdata-extremes', ['--no-party'],
      files={'userdata.json': json.dumps({'settings': {'noteSpeed': 1e308, 'musicVolume': -1e308,
                                                       'uiScale': 1e308, 'judgePerfectMs': -1e9},
                                          'scores': [], 'account': {}})})
    d('data-userdata-nan', ['--no-party'],
      files={'userdata.json': '{"settings": {"noteSpeed": NaN, "musicVolume": Infinity}}'})
    d('data-index-garbage', ['--no-party'], files={'profiles/index.json': '###'})
    d('data-index-empty-users', ['--no-party'], files={'profiles/index.json': '{"active":"","users":[]}'})
    d('data-index-active-missing', ['--no-party'],
      files={'profiles/index.json': '{"active":"ghost","users":[]}'})
    d('data-index-duplicate-ids', ['--no-party'],
      files={'profiles/index.json': '{"active":"a","users":[{"id":"a","name":"A"},{"id":"a","name":"B"}]}'})
    d('data-index-path-traversal', ['--no-party'],
      files={'profiles/index.json': '{"active":"../../evil","users":[{"id":"../../evil","name":"X"}]}'})
    d('data-index-huge-name', ['--no-party'],
      files={'profiles/index.json': json.dumps({'active': 'a',
                                                'users': [{'id': 'a', 'name': 'N' * 100000}]})})
    d('data-profile-garbage', ['--no-party'],
      files={'profiles/index.json': '{"active":"a","users":[{"id":"a","name":"A"}]}',
             'profiles/a.json': '!!!!'})
    d('data-profile-empty', ['--no-party'],
      files={'profiles/index.json': '{"active":"a","users":[{"id":"a","name":"A"}]}',
             'profiles/a.json': ''})
    d('data-musics-garbage', ['--no-party'], files={'musics.json': 'nope'})
    d('data-musics-array', ['--no-party'], files={'musics.json': '[]'})
    d('data-musics-wrong-shape', ['--no-party'], files={'musics.json': '[1,2,3]'})
    d('data-musics-missing-fields', ['--no-party'], files={'musics.json': '[{"id":1}]'})
    d('data-musics-huge-ids', ['--no-party'],
      files={'musics.json': '[{"id":99999999999999999999,"title":"x"}]'})
    d('data-levels-garbage', ['--no-party'], files={'music-levels.json': '}{'})
    d('data-levels-wrong-shape', ['--no-party'], files={'music-levels.json': '5'})
    d('data-vocals-garbage', ['--no-party'], files={'music-vocals.json': 'x'})
    d('data-aliases-garbage', ['--no-party'], files={'music-aliases.json': 'x'})
    d('data-chartdl-garbage', ['--no-party'], files={'chartdl.json': '{,}'})
    # A single wrong-TYPED field: nlohmann's value() throws on a type mismatch, and
    # loadUserData catches everything silently - so everything after the bad field
    # is skipped and the next save writes those defaults back for good.
    d('data-userdata-typed-field', ['--no-party'],
      files={'userdata.json': '{"settings": {"noteSpeed": "abc", "bgmVolume": 0.25,'
                              ' "padRumble": 0.7, "offsetSec": 0.25}}'})
    d('data-userdata-typed-field-deep', ['--no-party'],
      files={'userdata.json': '{"settings": {"bgmVolume": 0.25, "windowMode": "windowed",'
                              ' "fpsLimit": 30, "noteSpeed": "abc"}}'})
    d('data-chart-json-garbage', ['--no-party'], files={'charts/0001.json': 'x'})
    d('data-chart-json-empty', ['--no-party'], files={'charts/0001.json': '{}'})
    d('data-sus-garbage-while-select', ['--no-party'],
      files={'charts/0001_master.sus': '\x00\x01\x02 broken'})

    # ---- ui: random pad sequences ------------------------------------------
    buttons = ['A', 'B', 'X', 'Y', 'LB', 'RB', 'START', 'BACK']
    screens = [
        ['--no-party'],
        ['--no-party', '--settings', '--settings-tab', '0'],
        ['--no-party', '--settings', '--settings-tab', '3'],
        ['--no-party', '--settings', '--settings-tab', '4'],
        ['--no-party', '--guess'],
        ['--no-party', '--profile'],
        ['--no-party', '--singer-panel'],
        ['--no-party', '--result-preview'],
    ]
    seqs = [
        'A', 'B', 'START', 'BACK', 'A,A', 'B,B', 'A,B', 'B,A',
        'START,B', 'BACK,A', 'A,A,A', 'B,B,B', 'LB,RB', 'RB,LB',
        'A,B,START', 'START,A,B', 'X,Y,A', 'BACK,BACK,START',
        'A,A,B,B,START,BACK', 'LB,LB,RB,RB',
    ]
    n = 0
    for sc in screens:
        for seq in seqs:
            n += 1
            tag = sc[-1] if sc[-1] not in ('0', ) else 'play'
            out.append(dict(name='ui-%02d-%s-%s' % (n, tag.replace('--', ''), seq.replace(',', '')),
                            group='ui', args=s(sc + ['--fake-pad', seq], '2.0')))
            if n >= 72:
                break
        if n >= 72:
            break

    # ---- exit path: interactive, closed by WM_CLOSE ------------------------
    add('exit-close-normally', 'exit', ['--no-party', '--instance', 'multi'], interact=6)
    add('exit-close-after-settings', 'exit',
        ['--no-party', '--instance', 'multi', '--settings'], interact=6)
    add('exit-close-after-partial-mutation', 'exit',
        ['--no-party', '--instance', 'multi'], interact=6,
        files={'userdata.json': '{"settings":{"noteSpeed":42}}'})
    # profileDataPath() is plain string concatenation, and the id comes straight
    # out of a file the player can edit - so an id of "../../escaped" points the
    # save at <sandbox>/../escaped.json, i.e. outside profiles/ entirely. Run
    # without --screenshot so the save actually happens on close.
    add('exit-profile-id-traversal', 'exit',
        ['--no-party', '--instance', 'multi'], interact=6,
        files={'profiles/index.json':
               '{"active":"../../escaped","users":[{"id":"../../escaped","name":"X"}]}'})
    return out


# ---------------------------------------------------------------------------
# Report
# ---------------------------------------------------------------------------
def summarize(results):
    hard = []
    for r in results:
        if r['group'] == 'baseline':
            continue
        if r.get('escaped'):
            hard.append((r, 'wrote outside the data directory: %s' % ', '.join(r['escaped'])))
        if r['hang']:
            hard.append((r, 'HANG: never exited'))
        elif r['code'] not in (0, None):
            hard.append((r, 'exit code %s (0x%X)' % (r['code'], r['code'] & 0xFFFFFFFF)))
        elif r.get('want_shot') and not r['shot']:
            hard.append((r, '--screenshot asked for, no file produced'))
    return hard


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--root', default=DEFAULT_ROOT)
    ap.add_argument('--only', default=None, help='substring filter on the case name')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--report', default=None)
    args = ap.parse_args()

    all_cases = cases()
    if args.list:
        for c in all_cases:
            log('%-38s %s' % (c['name'], ' '.join(c['args'])))
        return 0

    picked = [c for c in all_cases if not args.only or args.only in c['name']]
    log('[monkey] %d cases, root=%s' % (len(picked), args.root))
    sb, pristine = setup(args.root)
    log('[monkey] sandbox ready: %s' % sb)

    results = []
    dirty = False
    for i, case in enumerate(picked, 1):
        case['args'] = [a.replace('SHOT', os.path.join(args.root, 'shots', case['name'] + '.png'))
                        for a in case['args']]
        if case.get('interact'):
            r = run_interactive(case, args.root, sb, pristine, dirty)
        else:
            r = run_case(case, args.root, sb, pristine, dirty)
        # A case that wrote into charts/ has to be undone by the *next* one, or
        # its corruption silently becomes the baseline for everything after it.
        dirty = r.get('dirty_charts', False)
        results.append(r)
        flag = ''
        if r['hang']:
            flag = '  <-- HANG'
        elif r['code'] not in (0, None):
            flag = '  <-- exit %s' % r['code']
        elif r['lines']:
            flag = '  <-- %d log line(s)' % len(r['lines'])
        log('[%3d/%3d] %-38s %5.1fs%s' % (i, len(picked), r['name'], r['seconds'], flag))

    write_report(results, args)
    return 0


def run_interactive(case, root, sb, pristine, restore_charts):
    """Run without --screenshot (so the normal exit path runs), then ask the
    window to close with a plain taskkill - no /F, which posts WM_CLOSE and lets
    the app persist its profile the way a real close does."""
    name = case['name']
    restore(sb, pristine, restore_charts=restore_charts)
    apply_mutations(sb, case.get('files'))
    exe = os.path.join(sb, 'cppsekai.exe')
    started = time.time()
    proc = subprocess.Popen([exe] + list(case['args']), cwd=sb,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    time.sleep(case.get('interact', 6))
    killed = False
    subprocess.run(['taskkill', '/PID', str(proc.pid)],
                   stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        out = proc.communicate(timeout=12)[0]
    except subprocess.TimeoutExpired:
        proc.kill()
        out = proc.communicate()[0]
        killed = True
    text = read_log(sb, out)
    hits = []
    for line in text.splitlines():
        stripped = line.strip()
        # Otherwise a signature like "nan" matches the `screenshot saved: ...`
        # line of every case whose name contains it.
        if not stripped or stripped.startswith('screenshot saved'):
            continue
        low = stripped.lower()
        for sig in SIGNATURES:
            if sig.lower() in low:
                hits.append(stripped[:220])
                break
    saved = 0
    for probe in ['userdata.json']:
        if os.path.isfile(os.path.join(sb, probe)):
            saved += 1
    prof = os.path.join(sb, 'profiles')
    if os.path.isdir(prof):
        saved += len([f for f in os.listdir(prof) if f.endswith('.json')])
    # Anything that landed *outside* the sandbox exe dir is a write that escaped
    # the data directory - the traversal case's whole point.
    parent = os.path.dirname(sb)
    escaped = sorted(f for f in os.listdir(parent) if f.endswith('.json'))
    note = 'persisted %d json file(s) on close' % saved if saved else 'NOTHING persisted on close'
    if escaped:
        note += '; WROTE OUTSIDE THE DATA DIR: %s' % ', '.join(escaped)
        for f in escaped:
            os.remove(os.path.join(parent, f))
    return {'name': name, 'group': case.get('group', '?'), 'args': case['args'],
            'code': None if killed else proc.returncode, 'hang': killed,
            'seconds': round(time.time() - started, 2),
            'note': note, 'escaped': escaped,
            'lines': hits[:12], 'shot': None, 'want_shot': False,
            'mutated': sorted((case.get('files') or {}).keys()),
            'dirty_charts': False}


def write_report(results, args):
    path = args.report or os.path.join(args.root, 'report.md')
    hard = summarize(results)
    by_group = {}
    for r in results:
        by_group.setdefault(r['group'], []).append(r)

    lines = []
    lines.append('# cppsekai 猴子测试报告')
    lines.append('')
    lines.append('日期：%s ｜ 用例 %d 个 ｜ 沙箱：`%s`' % (time.strftime('%Y-%m-%d %H:%M'), len(results), args.root))
    lines.append('')
    lines.append('沙箱是一份独立的 exe + assets + charts 副本，数据文件（userdata.json / '
                 'profiles/）都在沙箱内生成，仓库根的 profiles/ 全程没被碰过。')
    lines.append('')
    lines.append('## 结论速览')
    lines.append('')
    lines.append('| 类别 | 用例 | 硬失败（崩溃/挂死/没出图） | 有可疑日志 |')
    lines.append('|---|---|---|---|')
    for g in ['baseline', 'cli', 'file', 'data', 'ui', 'exit']:
        rs = by_group.get(g, [])
        if not rs:
            continue
        h = [r for r in rs if r in [x[0] for x in hard]]
        noisy = [r for r in rs if r['lines'] and r not in h]
        lines.append('| %s | %d | %d | %d |' % (g, len(rs), len(h), len(noisy)))
    lines.append('')
    if hard:
        lines.append('## 硬失败')
        lines.append('')
        for r, why in hard:
            lines.append('### `%s`' % r['name'])
            lines.append('')
            lines.append('- 命令：`cppsekai.exe %s`' % ' '.join(r['args']))
            lines.append('- **%s**' % why)
            if r.get('mutated'):
                lines.append('- 注入的文件：%s' % ', '.join('`%s`' % m for m in r['mutated']))
            for ln in r['lines'][:6]:
                lines.append('- 日志：`%s`' % ln.replace('`', "'"))
            lines.append('')
    else:
        lines.append('## 硬失败')
        lines.append('')
        lines.append('无：所有用例都正常退出（或按预期出图）。')
        lines.append('')
    lines.append('## 出现可疑日志的用例')
    lines.append('')
    base_lines = set()
    for r in results:
        if r['group'] == 'baseline':
            base_lines |= set(r['lines'])
    for r in results:
        extra = [ln for ln in r['lines'] if ln not in base_lines]
        if not extra or r in [x[0] for x in hard]:
            continue
        lines.append('### `%s`（%s）' % (r['name'], r['group']))
        lines.append('')
        lines.append('- 命令：`cppsekai.exe %s`' % ' '.join(r['args']))
        if r.get('mutated'):
            lines.append('- 注入的文件：%s' % ', '.join('`%s`' % m for m in r['mutated']))
        for ln in extra[:6]:
            lines.append('- `%s`' % ln.replace('`', "'"))
        lines.append('')
    lines.append('## 全部用例')
    lines.append('')
    lines.append('| 用例 | 组 | 退出码 | 秒 | 出图 | 可疑行 |')
    lines.append('|---|---|---|---|---|---|')
    for r in results:
        lines.append('| %s | %s | %s | %.1f | %s | %d |' % (
            r['name'], r['group'], r['code'], r['seconds'],
            ('%d B' % r['shot']) if r['shot'] else '-', len(r['lines'])))
    lines.append('')
    exits = [r for r in results if r['group'] == 'exit']
    if exits:
        lines.append('## 关窗退出路径')
        lines.append('')
        lines.append('（无 `--screenshot`，跑几秒后用 `taskkill /PID`（不带 `/F`）发 WM_CLOSE，'
                     '让程序走正常退出、把 profile 写回去。）')
        lines.append('')
        lines.append('| 用例 | 退出码 | 结果 |')
        lines.append('|---|---|---|')
        for r in exits:
            lines.append('| %s | %s | %s |' % (r['name'], r['code'], r['note']))
        lines.append('')
    with open(path, 'w', encoding='utf-8') as fh:
        fh.write('\n'.join(lines))
    log('[monkey] report -> %s' % path)
    log('[monkey] hard failures: %d' % len(hard))


if __name__ == '__main__':
    sys.exit(main())
