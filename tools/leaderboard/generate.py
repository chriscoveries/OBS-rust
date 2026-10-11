#!/usr/bin/env python3
"""Generate leaderboard.json for obs-rust/obs-studio.

Usage: generate.py [--repo OWNER/REPO] [--out leaderboard.json]
Needs the `gh` CLI, authenticated (locally, or GH_TOKEN/GITHUB_TOKEN in CI).
Standard library only.

Crediting rules
---------------
Credit goes to the ORIGINATING contributor, not whoever rebased or merged.
  1. A merged PR credits its author (`author.login`).
  2. If the PR author is a "relay" (the repo owner, a bot or an agent account)
     the PR may carry someone else's work. Then the original person is
     credited instead, found from, in order:
       a. `Co-Authored-By:` trailers in the PR's commits and body. Emails
          like `login@users.noreply.github.com` or
          `123+login@users.noreply.github.com` map to that login; other
          emails/names are looked up through the GitHub user search and, if
          that fails, the name is kept as-is (no avatar). A trailer only
          counts if it is on at least half of the PR's commits (or in the
          body), so a large stacked PR carrying a few commits is not
          reassigned.
       b. Body text such as "Supersedes #N (by @x)", "rebase of #N by @x",
          "re-lands #N": the author of PR #N in this repo is credited.
     If nothing is found the relay itself is credited.
  3. Bots and AI agents (`[bot]` logins, is_bot, Claude/Anthropic/Copilot
     co-authors) are never credited or ranked. Each credited person gets one
     merged-PR credit per PR, however many signals name them.
  4. Ranking is by merged PRs ONLY. Each merged PR is credited to exactly one
     originator (the first found above; others named are logged). Issues and
     closed-unmerged PRs earn nothing (the unmerged original of a re-land is
     never counted, so the work counts once). Issue counts are kept in the
     JSON for reference only.
Completeness: all merged PRs are fetched with full pagination and checked
against GitHub's merged-PR total; any PR that cannot be attributed to a human
is logged, and any human who authored/co-authored commits in merged PRs but
ends with 0 credit is flagged in the log.
"""
import argparse, json, re, subprocess, sys
from datetime import datetime, timezone

RELAYS = {"zackees"}  # repo owner; also anything treated as a bot
W_PR = 1
BOT_RE = re.compile(r"(\[bot\]$|^(claude|copilot|dependabot|github-actions|coderabbit)|anthropic|noreply@github\.com$)", re.I)
TRAILER = re.compile(r"(?im)^\s*co-authored-by:\s*(.+?)\s*<([^>]*)>")
REFS = re.compile(r"(?i)(?:supersedes|superseding|re-?lands?|rebase of|rebased from|carr(?:y|ies) over)\s+#(\d+)")
NOREPLY = re.compile(r"^(?:\d+\+)?([^@]+)@users\.noreply\.github\.com$", re.I)

def gh(*a):
    r = subprocess.run(["gh", *a], capture_output=True, text=True)
    if r.returncode:
        raise SystemExit(f"gh {' '.join(a)} failed: {r.stderr}")
    return r.stdout

def is_bot(login, email=""):
    return bool(BOT_RE.search(login or "") or BOT_RE.search(email or ""))

_cache = {}
def resolve(name, email, repo):
    """Map a co-author to a GitHub login (or None)."""
    m = NOREPLY.match(email)
    if m:
        return m.group(1)
    key = (name, email)
    if key not in _cache:
        login = None
        for q in (f"{email} in:email", f"{name} in:fullname"):
            try:
                d = json.loads(gh("api", "-X", "GET", "search/users", "-f", f"q={q}"))
                if d.get("total_count") == 1:
                    login = d["items"][0]["login"]; break
            except SystemExit:
                pass
        _cache[key] = login
    return _cache[key]

def pr_commits(repo, n):
    """All commits of a PR via paginated REST (no 100-commit cap)."""
    raw = gh("api", "--paginate", "--slurp", f"repos/{repo}/pulls/{n}/commits")
    out = []
    for page in json.loads(raw):
        for c in page:
            msg = c["commit"]["message"]
            head, _, body = msg.partition("\n")
            authors = []
            for k, who in (("author", c["commit"]["author"]), ("committer", c["commit"]["committer"])):
                if k == "author":
                    authors.append({"login": (c.get("author") or {}).get("login", ""), "email": who.get("email", "")})
            out.append({"messageHeadline": head, "messageBody": body, "authors": authors})
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--repo", default="obs-rust/obs-studio")
    ap.add_argument("--out", default="leaderboard.json")
    o = ap.parse_args()
    repo = o.repo
    # gh pr/issue list paginates internally; the limit is far above any real count.
    prs = json.loads(gh("pr", "list", "-R", repo, "--state", "all", "-L", "1000000", "--json",
                        "number,state,author,body,mergedAt,createdAt"))
    issues = json.loads(gh("issue", "list", "-R", repo, "--state", "all", "-L", "1000000", "--json",
                           "number,author,createdAt"))
    merged = [p for p in prs if p["state"] == "MERGED"]
    total = json.loads(gh("api", "-X", "GET", "search/issues", "-f",
                          f"q=repo:{repo} is:pr is:merged", "-f", "per_page=1"))["total_count"]
    if total != len(merged):
        raise SystemExit(f"INCOMPLETE: fetched {len(merged)} merged PRs, GitHub reports {total}")
    by_num = {p["number"]: p for p in prs}
    people = {}
    def person(login):
        return people.setdefault(login, {"login": login, "avatar_url": f"https://github.com/{login}.png?size=200",
                                         "merged_prs": 0, "issues_filed": 0, "prs": [], "issues": [], "dates": []})
    notes, unattributed, seen_humans = [], [], set()
    assigned = {}
    for p in sorted(merged, key=lambda p: p["number"]):
        a = p["author"] or {}
        login = a.get("login", "")
        commits = pr_commits(repo, p["number"])
        for c in commits:
            for au in c.get("authors") or []:
                if au.get("login") and not is_bot(au["login"], au.get("email", "")):
                    seen_humans.add(au["login"])
        if login and not (a.get("is_bot") or is_bot(login)):
            seen_humans.add(login)
        credited = []
        if login in RELAYS or a.get("is_bot") or is_bot(login):
            seen = {}
            for c in commits:
                msg = (c.get("messageHeadline", "") + "\n" + (c.get("messageBody") or ""))
                for name, email in {(n_, e_) for n_, e_ in TRAILER.findall(msg)}:
                    if not is_bot(name, email):
                        seen[(name, email)] = seen.get((name, email), 0) + 1
            for name, email in TRAILER.findall(p["body"] or ""):
                if not is_bot(name, email):
                    seen[(name, email)] = len(commits)
            for (name, email), cnt in seen.items():
                if cnt * 2 < len(commits):  # a stacked PR that merely carries a few commits
                    continue
                l = resolve(name, email, repo) or name
                seen_humans.add(l)
                if l != login and l not in credited:
                    credited.append(l)
            for n in map(int, REFS.findall(p["body"] or "")):
                src = by_num.get(n)
                l = ((src or {}).get("author") or {}).get("login")
                if l and l != login and not is_bot(l) and l not in credited:
                    credited.append(l)
        if not credited:
            if is_bot(login) or a.get("is_bot") or not login:
                unattributed.append(p["number"])
                continue
            credited = [login]
        elif len(credited) > 1:
            notes.append(f"#{p['number']}: also named {', '.join(credited[1:])} (credited {credited[0]} only)")
        l = credited[0]
        if login != l:
            notes.append(f"#{p['number']} by {login} -> credited {l}")
        assigned[p["number"]] = l
        q = person(l)
        q["merged_prs"] += 1
        q["prs"].append(p["number"])
        q["dates"].append(p["mergedAt"])
    for i in issues:
        l = (i["author"] or {}).get("login", "")
        if not l or is_bot(l) or (i["author"] or {}).get("is_bot"):
            continue
        if l in people:
            people[l]["issues_filed"] += 1
            people[l]["issues"].append(i["number"])
    rows = []
    for q in people.values():
        d = sorted(q.pop("dates"))
        q["score"] = W_PR * q["merged_prs"]
        q["first_contribution"], q["last_contribution"] = d[0], d[-1]
        q["pr_numbers"] = q.pop("prs"); q["issue_numbers"] = q.pop("issues")
        rows.append(q)
    rows.sort(key=lambda q: (-q["merged_prs"], q["first_contribution"], q["login"].lower()))
    for n, q in enumerate(rows, 1):
        q["rank"] = n
    # Completeness assertions
    assert len(assigned) + len(unattributed) == len(merged)
    assert sum(q["merged_prs"] for q in rows) == len(assigned)
    out = {"repo": repo, "generated_at": datetime.now(timezone.utc).strftime("%Y-%m-%dT%H:%M:%SZ"),
           "ranked_by": "merged_prs", "merged_pr_total": len(merged), "unattributed_prs": unattributed,
           "contributors": rows}
    with open(o.out, "w") as f:
        json.dump(out, f, indent=2); f.write("\n")
    print("\n".join(notes), file=sys.stderr)
    if unattributed:
        print(f"WARNING: {len(unattributed)} merged PRs unattributable to a human: {unattributed}", file=sys.stderr)
    zero = sorted(h for h in seen_humans if h not in people)
    if zero:
        print(f"WARNING: humans in merged PRs with 0 credit (check for a missed originator): {zero}", file=sys.stderr)
    print(f"wrote {o.out}: {len(rows)} contributors, {len(merged)} merged PRs, "
          f"{len(assigned)} attributed", file=sys.stderr)

if __name__ == "__main__":
    main()
