#!/usr/bin/env python3
"""render_test's event schedule: events applied in time order, none dropped.

Every gate in this suite times its events through render_test, which applies an
event on the first block that starts at or after its frame (<sec> x sr, truncated).
Two ways that could mislead a gate are checked for every key that schedules
events. The mod wheel's modwheelat= has the same checks in lfo_depth_gate.py's
[sched] case, which needs the LFO depth destinations to hear the wheel.

Rig: Cosmos holds note 60 on a dry patch with a 10 ms release, 1 s at 48 kHz in
512-frame blocks, so blocks start at multiples of 512 and the last one at frame
47616. polyat= and chanat= route Aftertouch to a low filter cutoff; loopat= and
playat= use a tempo-synced LFO on the amplitude (3 s renders where loopat= is
ordered).

  [order]  events of one key that fall due on the same block start are applied in
           time order, whatever order they are given in: every argument order
           renders byte-identical to the latest event alone, and each other event
           alone renders differently from it (the control). Three events where
           the payload has a range (setat, polyat, chanat, progat, loopat), the
           latest one's value between the other two, so neither argument order
           nor value order passes. noteon/noteoff (one schedule) and sustainat
           carry an on/off payload: two events, in both time orders.
  [tie]    events at the same time are applied in the order given: either order
           renders byte-identical to the event given second alone, and the two
           alone render differently. Also in a long schedule (32 setat= changes,
           the first 16 at the later time), since a sort that is not stable can
           keep short runs of equal times in order and still reorder long ones.
  [jumps]  loopat= jumps on different block starts: each one's shift is measured
           from the jumps before it in the schedule, so the order matters across
           blocks too. The reversed argument order renders byte-identical to the
           time order. Control: the time order with the second jump landing at
           -0.875 beats instead of 0.75, which is where the harness put it (byte
           for byte) from the reversed order before the schedule was sorted,
           renders differently.
  [reach]  an event on the last block start is applied and changes the render
           (the control, against the same render without it); one frame later no
           block start is left and the render is refused: exit code 1 (as
           _harness.render reports it), one line on stderr naming the key and the
           time, and no WAV. Every key but modwheelat=. playat= is compared as a
           time, not as a truncated frame: one at the last block start's time
           (0.992 s) is applied, one 0.48 of a frame later (0.99201 s, frame 47616
           when truncated) is refused. release= splits its block so that a block
           starts on its frame, wherever that lies in the render; it is refused
           when its frame is the one just past the end (a 1.00001 s render has
           48000.48 frames, of which 48000 are rendered).
  [split]  the block starts are the render loop's own, which release= moves: a
           release puts a setat= (and a playat=) out of reach that is reached
           without it, another brings a refused one within reach. In the setat=
           case an earlier change is applied, so the refusal does not hang on the
           schedule's first event.
  [report] every event that was never applied is reported, one line each, in the
           order the loop applies the keys and within a key in time order, and an
           applied event is not: one render with unapplied events of every key,
           the keys given in roughly the reverse order, two events late-first in
           every schedule. The order of notifyat=, panicat= and soundoffat= can
           show only here; a render cannot show it.
  [needs]  loopat= without songpos= and playat= with playing=0 can never be
           applied, whatever the block starts: refused with their own message,
           no WAV, while the same render with songpos= / playing=1 renders.

[order] and [tie] tell events apart by their payload, so they leave out notifyat=,
panicat= and soundoffat= (each of their events makes the same call); [reach] and
[report] cover those. Events of different keys due on the same block start are
applied in a fixed order whatever their times (see render_test.cpp); that is not
checked here.
"""
import filecmp
import itertools
import os
import sys

from _harness import OUT, render

SECONDS = 1.0
BLOCK = 512
SAME = ("0.300005", "0.30001", "0.300015")   # frames 14400.24 / .48 / .72 -> block 14848
TIE = "0.3"
EDGE = "0.9920125"     # frame 47616.6 -> 47616, the last block start
PAST = "0.99203"       # frame 47617.44 -> 47617: no block start left

BASE = dict(block=BLOCK, ampA=0.001, ampD=0.01, ampS=1, ampR=0.01, reverbOn=0,
            delayOn=0, chorusOn=0, cosmosChorus=0, driveOn=0, analogAmt=0, vintage=0,
            masterVol=-12)
# Aftertouch -> Filter Cutoff at full amount, cutoff parked low (polyat_gate's probe).
PRESSURE = dict(BASE, modSrc0=5, modDst0=5, modAmt0=1, filterCutoff=400, filterRes=0.2)
# LFO1 (triangle, synced, one cycle per beat at 120 BPM) -> Amplitude
# (lfo_sync_gate's rig), so the song position is audible as the LFO's phase.
SYNC = dict(BASE, osc1Wave=3, osc2Level=0, osc3Level=0, subLevel=0, noiseLevel=0,
            filterCutoff=12000, filterRes=0, filterEnvAmt=0, lfo1Shape=1, lfo1Rate=2,
            lfo1Fade=0, lfo1Sync=1, modSrc0=1, modDst0=7, modAmt0=1, tempo=120, playing=1)
LOOP = dict(SYNC, songpos=0)

failures = []


def check(name, ok, detail):
    print(f"{name}: {'PASS' if ok else 'FAIL'} ({detail})")
    if not ok:
        failures.append(name)


def params(events):
    """(key, value) pairs -> render() keyword arguments, in the order given.

    render() emits a list value as repeated key=value arguments, so one key's
    events must be contiguous for the argument order to survive."""
    p = {}
    for k, v in events:
        if k in p and list(p)[-1] != k:
            raise ValueError(f"{k} events are not contiguous: {events}")
        p.setdefault(k, []).append(v)
    return p


def name_of(tag):
    """The file name (without .wav) of the render run() makes for `tag`."""
    return "sg_" + "".join(c if c.isalnum() else "_" for c in tag)


def run(tag, events, rig=BASE, seconds=SECONDS):
    """Render Cosmos note 60 with `rig` plus `events`; the render's name.

    An event key that is also a rig key would replace the rig's value instead of
    adding to it, so that is refused rather than rendered."""
    p = params(events)
    clash = sorted(set(rig) & set(p))
    if clash:
        raise ValueError(f"events would replace the rig's {', '.join(clash)}: pass "
                         f"those as events too")
    render(0, 60, seconds, 1, name_of(tag), **dict(rig, **p))
    return name_of(tag)


def attempt(tag, events, rig=BASE, seconds=SECONDS):
    """run(), except that a failed render comes back as its error text (None when
    it rendered)."""
    try:
        run(tag, events, rig, seconds)
    except RuntimeError as e:
        return str(e)
    return None


def same(a, b):
    """Byte comparison of two renders written by run()."""
    return filecmp.cmp(os.path.join(OUT, a + ".wav"), os.path.join(OUT, b + ".wav"),
                       shallow=False)


def wav(tag):
    return os.path.exists(os.path.join(OUT, name_of(tag) + ".wav"))


def never(key, t, seconds=SECONDS, release=None):
    """render_test's line for an event it never applied."""
    split = f", release={float(release):.9g}" if release is not None else ""
    return (f"{key} {float(t):.9g} was never applied: no block starts at or after it "
            f"before the render ends at {seconds:.9g} (block={BLOCK}{split})")


def refused(tag, err, *lines):
    """Exit code 1 (as _harness.render reports it), exactly these stderr lines, and
    no WAV."""
    return (err == "render_test failed (rc=1): " + "\n".join(lines)) and not wav(tag)


def said(err):
    """A failed render's error text for a check's detail (its lines joined by " | "),
    nothing when it rendered."""
    return f" ({' | '.join(err.splitlines())})" if err else ""


# --- [order] ------------------------------------------------------------------

def case_order(name, events, rig=BASE, seconds=SECONDS):
    """`events` in time order, all due on one block start; the last is the latest."""
    latest = run(f"{name}_latest", events[-1:], rig, seconds)
    others = [run(f"{name}_alone{i}", [e], rig, seconds) for i, e in enumerate(events[:-1])]
    orders = list(itertools.permutations(events))
    matched = [same(run(f"{name}_order{i}", list(o), rig, seconds), latest)
               for i, o in enumerate(orders)]
    apart = all(not same(o, latest) for o in others)
    check(f"[order] {name}", all(matched) and apart,
          f"{len(events)} events due on one block start, byte-identical to the latest "
          f"alone in {matched.count(True)} of {len(orders)} argument orders; control "
          f"(each earlier one alone renders differently): {apart}")


def case_tie(name, first, second, rig=BASE, seconds=SECONDS):
    """Two events at the same time, both argument orders."""
    only_1 = run(f"{name}_tie_a", [first], rig, seconds)
    only_2 = run(f"{name}_tie_b", [second], rig, seconds)
    kept = (same(run(f"{name}_tie_ab", [first, second], rig, seconds), only_2)
            and same(run(f"{name}_tie_ba", [second, first], rig, seconds), only_1))
    apart = not same(only_1, only_2)
    check(f"[tie] {name}", kept and apart,
          f"the one given second is applied last, both ways round: {kept}; control "
          f"(the two alone render differently): {apart}")


def orders():
    s1, s2, s3 = SAME
    case_order("setat", [("setat", f"{s1}:masterVol:-30"), ("setat", f"{s2}:masterVol:0"),
                         ("setat", f"{s3}:masterVol:-20")])
    case_tie("setat", ("setat", f"{TIE}:masterVol:-30"), ("setat", f"{TIE}:masterVol:0"))

    # A long schedule: 32 changes due on one block start, the first 16 at the later
    # time. The 16th given, the last at the later time, is applied last. A sort that
    # is not stable can keep short runs of equal times in order and still reorder
    # long ones: libc++'s std::sort (Apple clang 21) kept them in order in all of
    # 20000 random schedules per size up to 23 events, and reordered most from 24 on.
    many = [("setat", f"{s2 if i < 16 else s1}:masterVol:{i - 40}") for i in range(32)]
    last = run("many_last", many[15:16])
    first = run("many_first", many[:1])
    kept = same(run("many", many), last)
    apart = not same(first, last)
    check("[tie] setat, 32 events", kept and apart,
          f"the last change given at the later time is applied last: {kept}; control "
          f"(the first change given at that time alone renders differently): {apart}")

    # noteon/noteoff share one schedule. A key-up for a key that is not down is a
    # no-op, so "on, then off" in one block renders as nothing happened at all.
    case_order("noteon-noteoff", [("noteon", f"{s1}:72"), ("noteoff", f"{s2}:72")])
    case_order("noteoff-noteon", [("noteoff", f"{s1}:72"), ("noteon", f"{s2}:72")])
    case_tie("noteon/noteoff", ("noteon", f"{TIE}:72"), ("noteoff", f"{TIE}:72"))

    # The pedal matters when the key goes up (0.5 s): down holds the note, up lets
    # it go.
    pedal = dict(BASE, noteoff="0.5:60")
    case_order("sustainat down-up", [("sustainat", f"{s1}:1"), ("sustainat", f"{s2}:0")],
               pedal)
    case_order("sustainat up-down", [("sustainat", f"{s1}:0"), ("sustainat", f"{s2}:1")],
               pedal)
    case_tie("sustainat", ("sustainat", f"{TIE}:1"), ("sustainat", f"{TIE}:0"), pedal)

    case_order("polyat", [("polyat", f"{s1}:60:1"), ("polyat", f"{s2}:60:0"),
                          ("polyat", f"{s3}:60:0.25")], PRESSURE)
    case_tie("polyat", ("polyat", f"{TIE}:60:0"), ("polyat", f"{TIE}:60:1"), PRESSURE)
    case_order("chanat", [("chanat", f"{s1}:1"), ("chanat", f"{s2}:0"),
                          ("chanat", f"{s3}:0.25")], PRESSURE)
    case_tie("chanat", ("chanat", f"{TIE}:0"), ("chanat", f"{TIE}:1"), PRESSURE)

    # Init Cosmos, Init Mono, Init Oracle.
    case_order("progat", [("progat", f"{s1}:36"), ("progat", f"{s2}:38"),
                          ("progat", f"{s3}:37")])
    case_tie("progat", ("progat", f"{TIE}:36"), ("progat", f"{TIE}:38"))

    # 3 s with songpos 0 at 120 BPM: beat = 2 x seconds. Times and targets are exact
    # binary fractions, so the latest jump alone computes the same position to the
    # last bit as the three in a row. 1.0, 1 + 2^-12 and 1 + 2^-11 s are frames
    # 48000, 48011 and 48023, all due on the block starting at 48128.
    case_order("loopat", [("loopat", "1.0:0.25"), ("loopat", "1.000244140625:0.75"),
                          ("loopat", "1.00048828125:0.5")], LOOP, seconds=3.0)
    case_tie("loopat", ("loopat", "1.0:0.25"), ("loopat", "1.0:0.75"), LOOP, seconds=3.0)


# --- [jumps] ------------------------------------------------------------------

def jumps():
    # At 1.0 s (beat 2) back to 0.375, at 2.0 s (beat 2.375 by then) back to 0.75.
    # Unsorted, the 2.0 s jump came first in the schedule and its shift was taken
    # without the 1.0 s one's: 4 - 0.75 = 3.25 instead of 1.625, and with the first
    # jump's 1.625 the playhead landed at 4 - 3.25 - 1.625 = -0.875 (4 being the
    # beat at 2.0 s without any jump).
    timed = run("jumps_time", [("loopat", "1.0:0.375"), ("loopat", "2.0:0.75")],
                LOOP, 3.0)
    rev = run("jumps_rev", [("loopat", "2.0:0.75"), ("loopat", "1.0:0.375")], LOOP, 3.0)
    unsorted = run("jumps_unsorted", [("loopat", "1.0:0.375"), ("loopat", "2.0:-0.875")],
                   LOOP, 3.0)
    check("[jumps] loopat on two block starts", same(rev, timed) and not same(timed, unsorted),
          f"the reversed argument order byte-identical to the time order: "
          f"{same(rev, timed)}; control (where the unsorted schedule put the playhead "
          f"renders differently): {not same(timed, unsorted)}")


# --- [reach] ------------------------------------------------------------------

def case_reach(name, key, edge, past, rig=BASE, past_time=PAST, seconds=SECONDS, pre=()):
    """`edge` is applied on the last block start, `past` is one frame later. The
    events in `pre` come first in all three renders."""
    pre = list(pre)
    none = run(f"{name}_none", pre, rig, seconds)
    on_edge = attempt(f"{name}_edge", pre + [(key, edge)], rig, seconds)
    past_edge = attempt(f"{name}_past", pre + [(key, past)], rig, seconds)
    applied = on_edge is None and not same(name_of(f"{name}_edge"), none)
    ok = refused(f"{name}_past", past_edge, never(key, past_time, seconds))
    check(f"[reach] {name}", ok and applied,
          f"one frame past the last block start: refused with its own line, no WAV: "
          f"{ok}{said(past_edge)}; control (on the last block start it is applied and "
          f"changes the render): {applied}{said(on_edge)}")


def reach():
    case_reach("setat", "setat", f"{EDGE}:masterVol:0", f"{PAST}:masterVol:0")
    case_reach("noteon", "noteon", f"{EDGE}:72", f"{PAST}:72")
    case_reach("noteoff", "noteoff", f"{EDGE}:60", f"{PAST}:60")
    # notifyProgramChange makes a parameter write land instead of glide.
    case_reach("notifyat", "notifyat", EDGE, PAST, dict(BASE, setat=f"{EDGE}:masterVol:0"))
    # Pedal down at 0.1 s, key up at 0.5 s: the note sounds until the pedal comes up.
    case_reach("sustainat", "sustainat", f"{EDGE}:0", f"{PAST}:0",
               dict(BASE, noteoff="0.5:60"), pre=[("sustainat", "0.1:1")])
    case_reach("panicat", "panicat", EDGE, PAST)
    case_reach("soundoffat", "soundoffat", EDGE, PAST)
    case_reach("polyat", "polyat", f"{EDGE}:60:1", f"{PAST}:60:1", PRESSURE)
    case_reach("chanat", "chanat", f"{EDGE}:1", f"{PAST}:1", PRESSURE)
    case_reach("progat", "progat", f"{EDGE}:37", f"{PAST}:37")
    case_reach("loopat", "loopat", f"{EDGE}:0.5", f"{PAST}:0.5", LOOP)

    # playat= against the transport stopped throughout (playing=0: the engine then
    # ignores the song position, which renders byte-identical to no songpos=).
    stopped = run("playat_none", [], dict(LOOP, playing=0))
    on_edge = attempt("playat_edge", [("playat", "0.992")], LOOP)
    past_edge = attempt("playat_past", [("playat", "0.99201")], LOOP)
    applied = on_edge is None and not same(name_of("playat_edge"), stopped)
    ok = refused("playat_past", past_edge, never("playat", "0.99201"))
    check("[reach] playat", ok and applied,
          f"0.48 of a frame past the last block start's time: refused with its own "
          f"line, no WAV: {ok}{said(past_edge)}; control (at that time the transport "
          f"starts, and the render differs from one stopped throughout): "
          f"{applied}{said(on_edge)}")

    # A 1.00001 s render: 48000.48 frames, of which 0..47999 are rendered.
    # 1.000005 s is frame 48000, the one just past the end.
    frac = 1.00001
    none = run("release_none", [], BASE, frac)
    inside = attempt("release_in", [("release", "0.99")], BASE, frac)
    past_end = attempt("release_past", [("release", "1.000005")], BASE, frac)
    applied = inside is None and not same(name_of("release_in"), none)
    ok = refused("release_past", past_end,
                 never("release", "1.000005", frac, release="1.000005"))
    check("[reach] release", ok and applied,
          f"on the frame just past the end: refused with its own line, no WAV: "
          f"{ok}{said(past_end)}; control (inside the render it is applied and changes "
          f"it): {applied}{said(inside)}")


# --- [split] ------------------------------------------------------------------

def split():
    # release=0.5 (frame 24000) splits a block, and the starts after it are
    # 24000 + 512 k, the last one 47552. 0.9915 s is frame 47592: reached at 47616
    # without the split, out of reach with it. release=0.999 (frame 47952) puts a
    # start inside the last block, which reaches frame 47617 (0.99203 s).
    early = ("setat", "0.25:masterVol:-20")
    whole = attempt("split_whole", [early, ("setat", "0.9915:masterVol:0")], BASE)
    cut = attempt("split_cut", [early, ("setat", "0.9915:masterVol:0")],
                  dict(BASE, release=0.5))
    cut_ok = refused("split_cut", cut, never("setat", "0.9915", release="0.5"))
    reached = attempt("split_reached", [("setat", f"{PAST}:masterVol:0")],
                      dict(BASE, release=0.999))
    check("[split] setat and release=",
          cut_ok and whole is None and reached is None,
          f"a change the split puts out of reach is refused: {cut_ok}{said(cut)}; control "
          f"(renders without release=): {whole is None}{said(whole)}; a change only the "
          f"split reaches renders: {reached is None}{said(reached)}")

    # The same for playat=, as a time: the last start is 47552 / 48000 = 0.99067 s
    # with release=0.5, 0.992 s without it, and 0.999 s with release=0.999.
    p_cut = attempt("split_play_cut", [("playat", "0.9915")], dict(LOOP, release=0.5))
    p_cut_ok = refused("split_play_cut", p_cut, never("playat", "0.9915", release="0.5"))
    p_whole = attempt("split_play_whole", [("playat", "0.9915")], LOOP)
    p_reached = attempt("split_play_reached", [("playat", "0.99201")],
                        dict(LOOP, release=0.999))
    check("[split] playat and release=",
          p_cut_ok and p_whole is None and p_reached is None,
          f"a start the split puts out of reach is refused: {p_cut_ok}{said(p_cut)}; "
          f"control (renders without release=): {p_whole is None}{said(p_whole)}; a "
          f"start only the split reaches renders: {p_reached is None}{said(p_reached)}")


# --- [report] -----------------------------------------------------------------

def report():
    # Events of every key past the last block start (and a release= on the frame
    # just past the end of a 1.00001 s render), the keys given in roughly the
    # reverse of the loop's order and two events late-first in each of the ten
    # schedules (noteon/noteoff share one, and so do polyat/chanat), with an
    # applied change (0.25 s) among them. polyat= is on note 0, a real key.
    frac, rel = 1.00001, "1.000005"
    events = [("loopat", "0.9975:1"), ("loopat", "0.996:0.5"), ("playat", "0.995"),
              ("soundoffat", "0.9965"), ("soundoffat", "0.994"), ("panicat", "0.997"),
              ("panicat", "0.993"), ("release", rel), ("noteon", "0.9955:72"),
              ("noteoff", "0.995:60"), ("modwheelat", "0.999:0"),
              ("modwheelat", "0.998:1"), ("chanat", "0.999:1"), ("polyat", "0.995:0:1"),
              ("sustainat", "0.9985:0"), ("sustainat", "0.997:1"), ("notifyat", "0.9985"),
              ("notifyat", "0.996"), ("setat", "0.9925:masterVol:-6"),
              ("setat", "0.25:masterVol:-20"), ("setat", f"{PAST}:masterVol:0"),
              ("progat", "0.9975:38"), ("progat", "0.995:37")]
    err = attempt("report", events, LOOP, frac)
    want = [never(k, t, frac, release=rel) for k, t in
            (("progat", "0.995"), ("progat", "0.9975"), ("setat", PAST),
             ("setat", "0.9925"), ("notifyat", "0.996"), ("notifyat", "0.9985"),
             ("sustainat", "0.997"), ("sustainat", "0.9985"), ("polyat", "0.995"),
             ("chanat", "0.999"), ("modwheelat", "0.998"), ("modwheelat", "0.999"),
             ("noteoff", "0.995"), ("noteon", "0.9955"), ("release", rel),
             ("panicat", "0.993"), ("panicat", "0.997"), ("soundoffat", "0.994"),
             ("soundoffat", "0.9965"), ("playat", "0.995"), ("loopat", "0.996"),
             ("loopat", "0.9975"))]
    ok = refused("report", err, *want)
    check("[report] every key never applied, one change applied", ok,
          f"the {len(want)} lines in the loop's key order, each key in time order, the "
          f"applied change not among them: {ok}{said(err)}")


# --- [needs] ------------------------------------------------------------------

def needs():
    loop = [("loopat", "0.5:0.25")]
    no_song = attempt("needs_loop", loop, SYNC)
    with_song = attempt("needs_loop_song", loop, LOOP)
    loop_ok = refused("needs_loop", no_song,
                      "loopat needs songpos=: without it no song position is sent, so a "
                      "loop or seek is never applied")
    check("[needs] loopat without songpos=", loop_ok and with_song is None,
          f"refused with its own message, no WAV: {loop_ok}{said(no_song)}; control "
          f"(renders with songpos=): {with_song is None}{said(with_song)}")

    start = [("playat", "0.5")]
    stopped = attempt("needs_play", start, dict(LOOP, playing=0))
    rolling = attempt("needs_play_rolling", start, LOOP)
    play_ok = refused("needs_play", stopped,
                      "playat needs playing=1: with playing=0 the transport never starts")
    check("[needs] playat with playing=0", play_ok and rolling is None,
          f"refused with its own message, no WAV: {play_ok}{said(stopped)}; control "
          f"(renders with playing=1): {rolling is None}{said(rolling)}")


def main():
    try:
        orders()
        jumps()
        reach()
        split()
        report()
        needs()
    except RuntimeError as e:
        # A render the checks expected to succeed failed (for instance a stale
        # render_test that does not know a key); report it, not a traceback.
        print(f"sched_gate: FAIL (render harness error: {e})")
        sys.exit(1)
    print(f"sched_gate: {'PASS' if not failures else 'FAIL'} ({len(failures)} failed checks)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
