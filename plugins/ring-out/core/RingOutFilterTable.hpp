// Copyright (C) 2026 Dusk Audio — GNU GPL v3.0 or later (see repository LICENSE).
// Third-party components in the built plugins (DAF — ISC; Dear ImGui — MIT; and
// others) are attributed in plugins/shared-daf/THIRD_PARTY_LICENSES.md.
//
// RingOutFilterTable.hpp — the notch table Ring Out keeps, its legal value grid
// and its text form.
//
// Shared by the DSP core, the DAF shell, the UI and the tests, so every one of
// them agrees on three things: how many filters there are, what values a filter
// may take (ranges and step grids), and how the table reads and writes as a
// string (which is what the host saves and what a user preset file carries).
//
// The grid is the reference control surface: CUT in 0.1 dB over -20..0,
// FREQ in 1 Hz steps to 500 Hz, 10 Hz to 1 kHz and 100 Hz to 20 kHz, Q in 0.1
// over 0.5..20. A filter made by the detection engine lands on the same grid.
//
// Everything here is allocation-free and locale-independent: the serialiser
// prints its own decimals and the parser reads its own, because plugin hosts
// call setlocale() and a comma-decimal locale would otherwise turn "66.3" into
// 66 on the way in and "66,3" on the way out.
#pragma once

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>

namespace duskaudio { namespace ringout {

constexpr int   kMaxFilters   = 20;

constexpr float kFreqMin      = 24.0f;
constexpr float kFreqMax      = 20000.0f;
constexpr float kFreqDefault  = 24.0f;
constexpr float kCutMin       = -20.0f;
constexpr float kCutMax       = 0.0f;
constexpr float kCutDefault   = 0.0f;
constexpr float kCutStep      = 0.1f;
constexpr float kQMin         = 0.5f;
constexpr float kQMax         = 20.0f;
constexpr float kQDefault     = 2.5f;
constexpr float kQStep        = 0.1f;

constexpr float kGlobalQMin      = 0.2f;
constexpr float kGlobalQMax      = 10.0f;
constexpr float kGlobalQDefault  = 1.0f;
constexpr float kGlobalAmpMin    = -24.0f;
constexpr float kGlobalAmpMax    = 24.0f;
constexpr float kGlobalAmpDefault = 0.0f;
constexpr float kGainOutMin      = -24.0f;
constexpr float kGainOutMax      = 24.0f;
constexpr float kGainOutDefault  = 0.0f;

// The deepest cut and the widest Q the detection engine will reach on its own.
// The reference states both as its limits: past them, feedback means the gain
// structure or the system is at fault, not the filters.
constexpr float kAutoCutFloor = -20.0f;
constexpr float kAutoQFloor   = 0.7f;

// The effective (table + global offset) cut is bounded here so a global AMP of
// -24 dB on a -20 dB filter stays a notch and never a boost.
constexpr float kEffectiveCutMin = -60.0f;
constexpr float kEffectiveCutMax = 0.0f;

constexpr float kSetupSeconds = 60.0f;
// An ADD search started by a controller tap (nobody will release it) gives up
// after this. A search the editor holds runs for as long as the editor keeps
// renewing its lease, which it does every half second while the button is
// down; a lease that lapses (editor closed mid-press, message lost) ends the
// search within kAddLeaseSeconds.
constexpr float kAddSeconds = 10.0f;
constexpr float kAddLeaseSeconds = 2.0f;

struct Filter
{
    bool  on     = false;
    float freqHz = kFreqDefault;
    float cutDb  = kCutDefault;
    float q      = kQDefault;
};

inline Filter defaultFilter() noexcept
{
    Filter f;
    f.on = true;
    return f;
}

struct FilterTable
{
    int      count = 0;             // filters 0..count-1 exist; rows are contiguous
    Filter   f[kMaxFilters] = {};
    // Row identity, moved with the row by add/remove/clear and nothing else:
    // the engine's filter slots follow it so a deleted row's neighbours keep
    // their slots. 0 = none assigned yet. Not part of the text form or of
    // tablesEqual(): two tables with the same filters are the same table.
    uint32_t id[kMaxFilters] = {};

    void clear() noexcept
    {
        count = 0;
        for (Filter& x : f) x = Filter();
        for (uint32_t& i : id) i = 0;
    }

    // Appends; returns the new row or -1 when full.
    int add(const Filter& x, uint32_t rowId = 0) noexcept
    {
        if (count >= kMaxFilters) return -1;
        f[count] = x;
        id[count] = rowId;
        return count++;
    }

    // Removes row i and closes the gap (the indicators move left).
    bool remove(int i) noexcept
    {
        if (i < 0 || i >= count) return false;
        for (int k = i; k + 1 < count; ++k) { f[k] = f[k + 1]; id[k] = id[k + 1]; }
        --count;
        f[count] = Filter();
        id[count] = 0;
        return true;
    }
};

//------------------------------------------------------------------------------
// value grid

inline float clampf(float v, float lo, float hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }

inline float snapToStep(float v, float step) noexcept
{
    return std::round(v / step) * step;
}

inline float snapCut(float c) noexcept
{
    return clampf(snapToStep(c, kCutStep), kCutMin, kCutMax);
}

inline float snapQ(float q) noexcept
{
    return clampf(snapToStep(q, kQStep), kQMin, kQMax);
}

// Increment of the FREQ control at `f`: 1 Hz below 500 Hz, 10 Hz to 1 kHz,
// 100 Hz above. These are the + / - step sizes, NOT a grid the value lives on:
// the detection engine places a filter at the exact tone (66.3 Hz on the
// reference's own display), and the control then nudges from there.
inline float freqStepAt(float f) noexcept
{
    if (f < 500.0f) return 1.0f;
    if (f < 1000.0f) return 10.0f;
    return 100.0f;
}

// Clamp to range and keep a tenth of a hertz, which is what the read-out shows.
inline float snapFreq(float f) noexcept
{
    return clampf(snapToStep(f, 0.1f), kFreqMin, kFreqMax);
}

inline float stepFreq(float f, int dir) noexcept
{
    f = snapFreq(f);
    if (dir > 0)
        return snapFreq(f + freqStepAt(f));
    if (dir < 0)
    {
        // Stepping down from a band edge uses the finer increment below it:
        // 1000 -> 990, 500 -> 499.
        const float below = std::nextafter(f, 0.0f);
        return snapFreq(f - freqStepAt(below < kFreqMin ? kFreqMin : below));
    }
    return f;
}

inline float stepCut(float c, int dir) noexcept
{
    return snapCut(snapCut(c) + (float)dir * kCutStep);
}

inline float stepQ(float q, int dir) noexcept
{
    return snapQ(snapQ(q) + (float)dir * kQStep);
}

inline bool filterValid(const Filter& x) noexcept
{
    return std::isfinite(x.freqHz) && std::isfinite(x.cutDb) && std::isfinite(x.q)
        && x.freqHz >= kFreqMin - 1.0e-3f && x.freqHz <= kFreqMax + 1.0e-3f
        && x.cutDb >= kCutMin - 1.0e-3f && x.cutDb <= kCutMax + 1.0e-3f
        && x.q >= kQMin - 1.0e-3f && x.q <= kQMax + 1.0e-3f;
}

inline bool tableValid(const FilterTable& t) noexcept
{
    if (t.count < 0 || t.count > kMaxFilters) return false;
    for (int i = 0; i < t.count; ++i)
        if (!filterValid(t.f[i])) return false;
    return true;
}

inline bool filtersEqual(const Filter& a, const Filter& b) noexcept
{
    return a.on == b.on && a.freqHz == b.freqHz && a.cutDb == b.cutDb && a.q == b.q;
}

inline bool tablesEqual(const FilterTable& a, const FilterTable& b) noexcept
{
    if (a.count != b.count) return false;
    for (int i = 0; i < a.count; ++i)
        if (!filtersEqual(a.f[i], b.f[i])) return false;
    return true;
}

//------------------------------------------------------------------------------
// text form
//
//   "<on>,<freq>,<cut>,<q>;<on>,<freq>,<cut>,<q>;..."     e.g. "1,66.3,-10.7,5.8;0,901,-8,5"
//
// An empty string is the empty table. Numbers are printed with up to three
// decimals and no exponent; the parser accepts an optional sign, digits, an
// optional fraction. Anything else fails the whole parse: a half-applied table
// is worse than a rejected one.

namespace detail
{
    // Writes `v` with `decimals` places into out (no exponent, '.' decimal
    // point, no locale). Returns characters written, or 0 if it did not fit.
    inline int formatFixed(float v, int decimals, char* out, int cap) noexcept
    {
        if (cap < 2 || !std::isfinite(v)) return 0;
        int n = 0;
        if (v < 0.0f) { out[n++] = '-'; v = -v; }
        double scale = 1.0;
        for (int i = 0; i < decimals; ++i) scale *= 10.0;
        const double scaled = std::round((double)v * scale);
        const uint64_t whole = (uint64_t)(scaled / scale);
        const uint64_t frac  = (uint64_t)(scaled - (double)whole * scale);
        // whole part
        char tmp[24]; int tn = 0;
        uint64_t w = whole;
        do { tmp[tn++] = (char)('0' + (w % 10)); w /= 10; } while (w != 0 && tn < 20);
        if (n + tn >= cap) return 0;
        while (tn > 0) out[n++] = tmp[--tn];
        if (decimals > 0)
        {
            // Trim trailing zeros from the fraction; print nothing when it is 0.
            uint64_t f = frac; int fd = decimals;
            while (fd > 0 && (f % 10) == 0) { f /= 10; --fd; }
            if (fd > 0)
            {
                if (n + 1 + fd >= cap) return 0;
                out[n++] = '.';
                char ftmp[24];
                for (int i = fd - 1; i >= 0; --i) { ftmp[i] = (char)('0' + (f % 10)); f /= 10; }
                for (int i = 0; i < fd; ++i) out[n++] = ftmp[i];
            }
        }
        out[n] = '\0';
        return n;
    }

    inline void skipSpace(const char*& p) noexcept
    {
        while (*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    }

    // Parses [+-]digits[.digits] at p; advances p past it. No exponent on
    // purpose: the writer never produces one and "1e999" must not get through.
    inline bool parseNumber(const char*& p, float& out) noexcept
    {
        const char* q = p;
        bool neg = false;
        if (*q == '+' || *q == '-') { neg = *q == '-'; ++q; }
        double v = 0.0; bool digit = false; int count = 0;
        while (*q >= '0' && *q <= '9') { v = v * 10.0 + (*q - '0'); ++q; digit = true; if (++count > 12) return false; }
        if (*q == '.')
        {
            ++q;
            double place = 0.1; count = 0;
            while (*q >= '0' && *q <= '9') { v += (*q - '0') * place; place *= 0.1; ++q; digit = true; if (++count > 12) return false; }
        }
        if (!digit) return false;
        out = (float)(neg ? -v : v);
        if (!std::isfinite(out)) return false;
        p = q;
        return true;
    }

    // One filter, "<on>,<freq>,<cut>,<q>", validated and landed on the grid.
    // Shared by the table and the edit-command grammars so the editor's
    // optimistic mirror and the plugin always parse the same text the same way.
    inline bool parseFilter(const char*& p, Filter& x) noexcept
    {
        if (*p != '0' && *p != '1') return false;
        x.on = *p++ == '1';
        if (*p++ != ',') return false;
        if (!parseNumber(p, x.freqHz)) return false;
        if (*p++ != ',') return false;
        if (!parseNumber(p, x.cutDb)) return false;
        if (*p++ != ',') return false;
        if (!parseNumber(p, x.q)) return false;
        if (!filterValid(x)) return false;
        x.freqHz = snapFreq(x.freqHz);
        x.cutDb  = snapCut(x.cutDb);
        x.q      = snapQ(x.q);
        return true;
    }
}

// Returns false when the buffer is too small (the table is left unwritten).
inline bool serializeTable(const FilterTable& t, char* buf, int cap) noexcept
{
    if (buf == nullptr || cap < 1) return false;
    int n = 0;
    buf[0] = '\0';
    for (int i = 0; i < t.count && i < kMaxFilters; ++i)
    {
        const Filter& x = t.f[i];
        if (i > 0) { if (n + 1 >= cap) return false; buf[n++] = ';'; }
        if (n + 2 >= cap) return false;
        buf[n++] = x.on ? '1' : '0';
        buf[n++] = ',';
        int w = detail::formatFixed(x.freqHz, 3, buf + n, cap - n); if (w == 0) return false; n += w;
        if (n + 1 >= cap) return false; buf[n++] = ',';
        w = detail::formatFixed(x.cutDb, 3, buf + n, cap - n); if (w == 0) return false; n += w;
        if (n + 1 >= cap) return false; buf[n++] = ',';
        w = detail::formatFixed(x.q, 3, buf + n, cap - n); if (w == 0) return false; n += w;
        buf[n] = '\0';
    }
    return true;
}

// Strict: the whole text must be a valid table or `out` is untouched.
inline bool parseTable(const char* text, FilterTable& out) noexcept
{
    FilterTable t;
    if (text == nullptr) return false;
    const char* p = text;
    detail::skipSpace(p);
    if (*p == '\0') { out = t; return true; }
    for (;;)
    {
        if (t.count >= kMaxFilters) return false;
        Filter x;
        if (!detail::parseFilter(p, x)) return false;
        t.f[t.count++] = x;
        detail::skipSpace(p);
        if (*p == '\0') break;
        if (*p++ != ';') return false;
        detail::skipSpace(p);
        if (*p == '\0') break;   // tolerate a trailing ';'
    }
    out = t;
    return true;
}

// Largest serialised table: 20 entries of "1,20000.000,-20.000,20.000;" style text.
constexpr int kTableTextCapacity = 1024;

//------------------------------------------------------------------------------
// the edit command channel (UI -> plugin, see RingOutPlugin.cpp "edit" state)
//
//   "set,<slot>,<on>,<freq>,<cut>,<q>[,<on>,<freq>,<cut>,<q>]"
//                                        edit one existing slot. With the second
//                                        filter (the row as the editor last saw
//                                        it) only the fields that differ between
//                                        the two are written, so a nudge of one
//                                        field cannot revert what the engine did
//                                        to the others meanwhile
//   "add,<on>,<freq>,<cut>,<q>"          append (ignored when full)
//   "del,<slot>"                         remove and close the gap
//   "clear"                              remove every filter
//
// and the engine buttons, which the editor also sends this way (a trigger
// parameter written from the editor can be swallowed by a host that forwards
// only control-port changes):
//
//   "setupon" / "setupoff"               arm the engine / disarm it (the editor's intent,
//                                        never a toggle decided from a stale status)
//   "addstart" / "addhold" / "addstop"   ADD pressed / still held (lease renewal) / released

struct EditCommand
{
    enum Kind { kNone, kSet, kAdd, kDelete, kClear, kSetupOn, kSetupOff, kAddStart, kAddHold, kAddStop };
    bool isTableEdit() const noexcept { return kind == kSet || kind == kAdd || kind == kDelete || kind == kClear; }
    Kind   kind = kNone;
    int    slot = -1;
    Filter filter;
    Filter previous;              // kSet only: the row as the sender last saw it
    bool   hasPrevious = false;
};

inline bool parseEditCommand(const char* text, EditCommand& out) noexcept
{
    EditCommand c;
    if (text == nullptr) return false;
    const char* p = text;
    const auto matchWord = [&p](const char* w) -> bool
    {
        const size_t n = std::strlen(w);
        if (std::strncmp(p, w, n) != 0) return false;
        p += n;
        return true;
    };
    const auto parseInt = [&p](int& v) -> bool
    {
        float f = 0.0f;
        if (!detail::parseNumber(p, f)) return false;
        if (f < 0.0f || f > 1000.0f || f != std::floor(f)) return false;
        v = (int)f;
        return true;
    };
    const auto parseFilter = [&p](Filter& x) -> bool { return detail::parseFilter(p, x); };

    if (matchWord("clear"))
        c.kind = EditCommand::kClear;
    else if (matchWord("addstop"))
        c.kind = EditCommand::kAddStop;
    else if (matchWord("addstart"))
        c.kind = EditCommand::kAddStart;
    else if (matchWord("addhold"))
        c.kind = EditCommand::kAddHold;
    else if (matchWord("setupon"))
        c.kind = EditCommand::kSetupOn;
    else if (matchWord("setupoff"))
        c.kind = EditCommand::kSetupOff;
    else if (matchWord("set,"))
    {
        c.kind = EditCommand::kSet;
        if (!parseInt(c.slot) || c.slot >= kMaxFilters) return false;
        if (*p++ != ',') return false;
        if (!parseFilter(c.filter)) return false;
        if (*p == ',')
        {
            ++p;
            if (!parseFilter(c.previous)) return false;
            c.hasPrevious = true;
        }
    }
    else if (matchWord("add,"))
    {
        c.kind = EditCommand::kAdd;
        if (!parseFilter(c.filter)) return false;
    }
    else if (matchWord("del,"))
    {
        c.kind = EditCommand::kDelete;
        if (!parseInt(c.slot) || c.slot >= kMaxFilters) return false;
    }
    else
        return false;

    detail::skipSpace(p);
    if (*p != '\0') return false;
    out = c;
    return true;
}

inline bool formatEditCommand(const EditCommand& c, char* buf, int cap) noexcept
{
    if (buf == nullptr || cap < 8) return false;
    int n = 0;
    const auto put = [&](const char* s) -> bool
    {
        const int l = (int)std::strlen(s);
        if (n + l >= cap) return false;
        std::memcpy(buf + n, s, (size_t)l); n += l; buf[n] = '\0';
        return true;
    };
    const auto putNum = [&](float v, int dec) -> bool
    {
        const int w = detail::formatFixed(v, dec, buf + n, cap - n);
        if (w == 0) return false;
        n += w; return true;
    };
    const auto putFilter = [&](const Filter& x) -> bool
    {
        return put(x.on ? "1," : "0,") && putNum(x.freqHz, 3) && put(",")
            && putNum(x.cutDb, 3) && put(",") && putNum(x.q, 3);
    };
    switch (c.kind)
    {
    case EditCommand::kClear:  return put("clear");
    case EditCommand::kSetupOn:  return put("setupon");
    case EditCommand::kSetupOff: return put("setupoff");
    case EditCommand::kAddStart: return put("addstart");
    case EditCommand::kAddHold:  return put("addhold");
    case EditCommand::kAddStop: return put("addstop");
    case EditCommand::kSet:
        if (!(put("set,") && putNum((float)c.slot, 0) && put(",") && putFilter(c.filter))) return false;
        return !c.hasPrevious || (put(",") && putFilter(c.previous));
    case EditCommand::kAdd:    return put("add,") && putFilter(c.filter);
    case EditCommand::kDelete: return put("del,") && putNum((float)c.slot, 0);
    default: return false;
    }
}

// Applies a parsed command to a table. Returns the slot the command touched
// (for selection follow-up) or -1 when nothing changed. The engine commands
// are not table edits and always return -1; the plugin acts on them directly.
inline int applyEditCommand(FilterTable& t, const EditCommand& c) noexcept
{
    switch (c.kind)
    {
    case EditCommand::kClear:
        if (t.count == 0) return -1;
        t.clear();
        return 0;
    case EditCommand::kSet:
    {
        if (c.slot < 0 || c.slot >= t.count) return -1;
        Filter& row = t.f[c.slot];
        const Filter before = row;
        if (!c.hasPrevious)
            row = c.filter;
        else
        {
            // Field-level merge: write only what the sender changed.
            if (c.filter.on != c.previous.on)         row.on = c.filter.on;
            if (c.filter.freqHz != c.previous.freqHz) row.freqHz = c.filter.freqHz;
            if (c.filter.cutDb != c.previous.cutDb)   row.cutDb = c.filter.cutDb;
            if (c.filter.q != c.previous.q)           row.q = c.filter.q;
        }
        return filtersEqual(before, row) ? -1 : c.slot;   // an unchanged row is no edit
    }
    case EditCommand::kAdd:
        return t.add(c.filter);
    case EditCommand::kDelete:
        return t.remove(c.slot) ? c.slot : -1;
    default:
        return -1;
    }
}

}} // namespace duskaudio::ringout
