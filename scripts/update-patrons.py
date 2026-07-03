#!/usr/bin/env python3
"""Regenerate plugins/shared/PatreonBackers.h tier arrays from the live Patreon campaign.

Run before tagging a release (or let CI run it — see .github/workflows/build.yml):

    scripts/update-patrons.py            # rewrite the header(s), show a summary
    scripts/update-patrons.py --dry-run  # show what would change, write nothing

One-time setup:
  1. Register an API client at https://www.patreon.com/portal/registration/register-clients
     (any redirect URI works — only the creator tokens are used).
  2. Copy the Creator's Access Token, Creator's Refresh Token, Client ID and
     Client Secret into ~/.config/dusk-audio/patreon.json:

        {
          "access_token":  "...",
          "refresh_token": "...",
          "client_id":     "...",
          "client_secret": "...",
          "name_overrides": { "API Full Name": "Preferred Display Name" }
        }

The script refreshes expired tokens automatically and rewrites that file with
the rotated pair, so setup is genuinely one-time.

Behaviour:
  - Active patrons are bucketed by their highest entitled tier amount:
    >= $10 champions, >= $5 patrons, >= $3 supporters, >= $1 hugs.
  - Anyone listed in the current header who is no longer an active patron is
    moved to pastSupporters (existing pastSupporters entries are preserved).
  - This repo's plugins/shared/PatreonBackers.h is rewritten. Sibling checkouts
    / worktrees that also carry the header (plugins-main / plugins-worktree /
    plugins-multi-synth, or a path in DUSK_PLUGINS_PATH) are kept in sync too,
    per the note in the header itself.
  - Only the array bodies are touched; the rest of the header is left as-is.

Committing the result is left to you — review the diff first.
"""

import argparse
import json
import os
import re
import sys
import tempfile
import urllib.error
import urllib.parse
import urllib.request
from pathlib import Path

API = "https://www.patreon.com/api/oauth2/v2"
TOKEN_URL = "https://www.patreon.com/api/oauth2/token"
CONFIG_PATH = Path.home() / ".config" / "dusk-audio" / "patreon.json"

TIERS = [  # (array name in the header, minimum cents)
    ("champions", 1000),
    ("patrons", 500),
    ("supporters", 300),
    ("hugs", 100),
]
ARRAY_NAMES = [name for name, _ in TIERS] + ["pastSupporters"]

# Path from this repo's root to the header.
HEADER_RELPATH = Path("plugins") / "shared" / "PatreonBackers.h"


def write_config_secure(config):
    """Atomically rewrite CONFIG_PATH with user-only (0600) permissions.

    The config holds access/refresh tokens and the client secret; a plain
    write_text() leaves the file at the process umask, which can be
    world-readable. Write to a temp file in the same dir, chmod 0600, fsync,
    then rename so a reader never sees a partial or permissive file.
    """
    data = json.dumps(config, indent=2) + "\n"
    fd, tmp = tempfile.mkstemp(dir=str(CONFIG_PATH.parent),
                               prefix=".patreon-", suffix=".tmp")
    fd_owned_by_file = False  # os.fdopen takes ownership only once it returns
    try:
        os.fchmod(fd, 0o600)
        with os.fdopen(fd, "w", encoding="utf-8") as f:
            fd_owned_by_file = True
            f.write(data)
            f.flush()
            os.fsync(f.fileno())
        os.replace(tmp, CONFIG_PATH)
    except BaseException:
        if not fd_owned_by_file:
            try:
                os.close(fd)
            except OSError:
                pass
        try:
            os.unlink(tmp)
        except OSError:
            pass
        raise


def api_get(url, token):
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {token}"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        return json.load(resp)


def refresh_tokens(config):
    body = urllib.parse.urlencode({
        "grant_type": "refresh_token",
        "refresh_token": config["refresh_token"],
        "client_id": config["client_id"],
        "client_secret": config["client_secret"],
    }).encode()
    req = urllib.request.Request(TOKEN_URL, data=body, method="POST")
    req.add_header("Content-Type", "application/x-www-form-urlencoded")
    with urllib.request.urlopen(req, timeout=30) as resp:
        fresh = json.load(resp)
    config["access_token"] = fresh["access_token"]
    config["refresh_token"] = fresh.get("refresh_token", config["refresh_token"])
    write_config_secure(config)
    print("Patreon tokens refreshed and saved.")
    return config


def fetch_members(config):
    """Returns (active: {name: max_cents}, former: set of names)."""
    def run(token):
        camps = api_get(f"{API}/campaigns", token)
        if not camps.get("data"):
            sys.exit("No campaign found for this token.")
        campaign_id = camps["data"][0]["id"]

        fields = urllib.parse.urlencode({
            "include": "currently_entitled_tiers",
            "fields[member]": "full_name,patron_status",
            "fields[tier]": "amount_cents",
            "page[count]": "500",
        })
        url = f"{API}/campaigns/{campaign_id}/members?{fields}"

        active, former = {}, set()
        while url:
            page = api_get(url, token)
            tier_cents = {
                inc["id"]: inc["attributes"].get("amount_cents", 0)
                for inc in page.get("included", [])
                if inc["type"] == "tier"
            }
            for m in page.get("data", []):
                name = (m["attributes"].get("full_name") or "").strip()
                if not name:
                    continue
                status = m["attributes"].get("patron_status")
                if status == "active_patron":
                    tiers = m.get("relationships", {}) \
                             .get("currently_entitled_tiers", {}) \
                             .get("data", [])
                    cents = max((tier_cents.get(t["id"], 0) for t in tiers),
                                default=0)
                    if cents >= 100:
                        active[name] = max(cents, active.get(name, 0))
                elif status in ("former_patron", "declined_patron"):
                    former.add(name)
            url = page.get("links", {}).get("next")
        return active, former

    try:
        return run(config["access_token"])
    except urllib.error.HTTPError as e:
        if e.code != 401:
            raise
        config = refresh_tokens(config)
        return run(config["access_token"])
    except urllib.error.URLError as e:
        # DNS failure / connection refused / unreachable network / timeout —
        # a clearer message than a raw traceback. HTTPError (a URLError
        # subclass) is handled above, so this only catches transport errors.
        sys.exit(f"Network error contacting Patreon ({e.reason}). "
                 "Check your connection and retry.")


def find_headers(repo_root):
    """Return every PatreonBackers.h to rewrite, canonical (this repo) first.

    Order matters: the dropped-to-pastSupporters merge and the drift check both
    read headers[0] as the source of truth, so this repo's own header must come
    first. Sibling worktrees / an explicit DUSK_PLUGINS_PATH are kept in sync
    behind it (see the sync note in the header).
    """
    found, seen = [], set()
    candidates = [repo_root / HEADER_RELPATH]  # canonical: this checkout

    env_path = os.environ.get("DUSK_PLUGINS_PATH")
    if env_path:
        candidates.append(Path(env_path) / HEADER_RELPATH)

    parent = repo_root.parent
    candidates += [parent / s / HEADER_RELPATH
                   for s in ("plugins-main", "plugins",
                             "plugins-worktree", "plugins-multi-synth")]

    for h in candidates:
        if not h.is_file():
            continue
        key = h.resolve()
        if key in seen:
            continue
        seen.add(key)
        found.append(h)
    return found


def cpp_escape(s):
    # Backslash first, then the rest, so a raw newline/CR in a display name
    # doesn't break the generated C++ string literal at compile time.
    return (s.replace("\\", "\\\\").replace('"', '\\"')
             .replace("\n", "\\n").replace("\r", "\\r"))


def cpp_unescape(s):
    # Single pass: chained str.replace() would let one substitution synthesise an
    # escape the next consumes (a literal "\\n" -> backslash+'n' would decode to a
    # newline). cpp_escape only emits \\ \" \n \r, so decode exactly those.
    out, i, n = [], 0, len(s)
    decode = {"\\": "\\", '"': '"', "n": "\n", "r": "\r"}
    while i < n:
        if s[i] == "\\" and i + 1 < n and s[i + 1] in decode:
            out.append(decode[s[i + 1]])
            i += 2
        else:
            out.append(s[i])
            i += 1
    return "".join(out)


def parse_array(text, name):
    m = re.search(
        rf"inline const std::vector<juce::String> {name}\s*=\s*\{{(.*?)\}};",
        text, re.DOTALL)
    if m is None:
        sys.exit(f"PatreonBackers.h: array '{name}' not found — "
                 "has the file's structure changed?")
    return [cpp_unescape(s)
            for s in re.findall(r'"((?:[^"\\]|\\.)*)"', m.group(1))]


def replace_array(text, name, names):
    body = "\n" + "".join(f'        "{cpp_escape(n)}",\n' for n in names) + "    " \
        if names else "\n    "
    return re.sub(
        rf"(inline const std::vector<juce::String> {name}\s*=\s*\{{).*?(\}};)",
        lambda m: m.group(1) + body + m.group(2),
        text, count=1, flags=re.DOTALL)


def main():
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--dry-run", action="store_true",
                    help="print the resulting tier lists, write nothing")
    args = ap.parse_args()

    if not CONFIG_PATH.is_file():
        sys.exit(f"Missing {CONFIG_PATH} — see the setup notes at the top "
                 "of this script.")
    # The file holds access/refresh tokens + the client secret. If it was created
    # with a permissive umask (group/world readable), tighten it to 0600 before
    # reading so the secrets aren't left exposed. (No-op on a file already 0600.)
    if CONFIG_PATH.stat().st_mode & 0o077:
        try:
            os.chmod(CONFIG_PATH, 0o600)
        except OSError:
            pass
    config = json.loads(CONFIG_PATH.read_text(encoding="utf-8"))
    missing = [k for k in ("access_token", "refresh_token",
                           "client_id", "client_secret") if k not in config]
    if missing:
        sys.exit(f"{CONFIG_PATH} is missing required key(s): "
                 f"{', '.join(missing)} — see the setup notes at the top "
                 "of this script.")
    overrides = config.get("name_overrides", {})

    repo_root = Path(__file__).resolve().parent.parent
    headers = find_headers(repo_root)
    if not headers:
        sys.exit(f"No PatreonBackers.h found (looked for {repo_root / HEADER_RELPATH}).")

    active, _former = fetch_members(config)
    # Apply display-name overrides, keeping the HIGHEST contribution when two
    # API names collapse to the same display name (a plain dict comprehension
    # would keep whichever happened to come last, dropping the higher tier).
    merged = {}
    for n, c in active.items():
        display = overrides.get(n, n)
        merged[display] = max(c, merged.get(display, 0))
    active = merged

    new_tiers = {name: [] for name, _ in TIERS}
    for name, cents in active.items():
        for tier_name, min_cents in TIERS:  # ordered high to low
            if cents >= min_cents:
                new_tiers[tier_name].append(name)
                break
    for v in new_tiers.values():
        v.sort(key=str.casefold)

    # The dropped-to-pastSupporters merge is computed from headers[0] only,
    # so refuse to run if the sibling worktree copies have drifted — a name
    # present only in a non-canonical copy would silently vanish.
    canonical = headers[0].read_text(encoding="utf-8")
    canonical_arrays = {n: parse_array(canonical, n) for n in ARRAY_NAMES}
    stale = [str(h) for h in headers[1:]
             if {n: parse_array(h.read_text(encoding="utf-8"), n)
                 for n in ARRAY_NAMES} != canonical_arrays]
    if stale:
        sys.exit("PatreonBackers.h copies are OUT OF SYNC with "
                 f"{headers[0]}:\n  " + "\n  ".join(stale)
                 + "\nReconcile the tier/pastSupporters arrays by hand first "
                 "(the merge of dropped names into pastSupporters reads only "
                 "the canonical copy, so running now could lose names).")

    # Past supporters: keep the existing list, and move in anyone listed in
    # the canonical header's tiers who is no longer an active patron.
    previously_listed = set()
    for name, _ in TIERS:
        previously_listed.update(parse_array(canonical, name))
    past = parse_array(canonical, "pastSupporters")
    now_active = set(active.keys())
    dropped = sorted((previously_listed - now_active) - set(past),
                     key=str.casefold)
    # Subtract now_active so a returning patron leaves pastSupporters
    # instead of being credited in two sections at once.
    new_past = sorted((set(past) | set(dropped)) - now_active, key=str.casefold)

    print("Active patrons by tier:")
    for tier_name, _ in TIERS:
        print(f"  {tier_name:14s} {', '.join(new_tiers[tier_name]) or '(none)'}")
    print(f"  pastSupporters {', '.join(new_past) or '(none)'}")
    if dropped:
        print(f"Moved to past supporters: {', '.join(dropped)}")

    if args.dry_run:
        print("\nDry run — nothing written.")
        return

    for header in headers:
        text = header.read_text(encoding="utf-8")
        for tier_name, _ in TIERS:
            text = replace_array(text, tier_name, new_tiers[tier_name])
        text = replace_array(text, "pastSupporters", new_past)
        # Atomic temp-file + rename so an interrupted run never leaves a
        # truncated header behind (same pattern as write_config_secure).
        fd, tmp = tempfile.mkstemp(dir=str(header.parent),
                                   prefix="." + header.name + ".", suffix=".tmp")
        fd_owned_by_file = False  # os.fdopen takes ownership only once it returns
        try:
            with os.fdopen(fd, "w", encoding="utf-8") as fh:
                fd_owned_by_file = True
                fh.write(text)
                fh.flush()
                os.fsync(fh.fileno())
            os.replace(tmp, header)
        except BaseException:
            if not fd_owned_by_file:
                try:
                    os.close(fd)
                except OSError:
                    pass
            try:
                os.unlink(tmp)
            except OSError:
                pass
            raise
        print(f"Updated {header}")

    print("\nReview + commit the change, then tag the release.")


if __name__ == "__main__":
    main()
