# Repository protection

`main` is protected by the ruleset in [`.github/rulesets/main-protection.json`](../.github/rulesets/main-protection.json) (ICS-001). The file is the reviewed source of truth; GitHub applies whatever was last imported, so re-import it after every change to the file.

## What the ruleset enforces on `main`

| Rule | Effect |
|---|---|
| Pull request required | No direct pushes; every change arrives through a pull request. |
| Two approvals | Two approving reviews, including one from the code owner in [`.github/CODEOWNERS`](../.github/CODEOWNERS). |
| Approval after the latest push | New commits dismiss earlier approvals; the last push must be approved by someone other than its author. |
| Resolved conversations | Every review thread must be resolved before merging. |
| Squash merge only | The only allowed merge method. |
| Required checks | "AI-assist declaration" and "AI-assist human review" must pass; see [ai-usage.md](ai-usage.md). |
| Signed commits | Every commit on `main` must carry a verified signature. |
| Linear history | No merge commits on `main`. |
| No force-push, no deletion | `main` cannot be rewritten or deleted. |

Signed commits and linear history together mean **Squash and merge** is the only button that works: GitHub signs the squash commit it creates, and rebase merging is not allowed. Commits on feature branches do not need signatures.

## Bypass while the team is one person

The repository admin role may bypass these rules **only when merging a pull request**, which lets a solo owner merge without two other reviewers. Direct pushes to `main` stay blocked for everyone, admins included. Remove the bypass once at least two other reviewers are code owners: delete the `bypass_actors` entry in the JSON file and re-import it.

## Import or update the ruleset

1. Download [`main-protection.json`](../.github/rulesets/main-protection.json) from the branch you are applying.
2. On GitHub, open **Settings → Rules → Rulesets**.
3. First time: choose **New ruleset → Import a ruleset** and select the file. To update: open **Protect main**, delete it, and import the new file.
4. Check that the ruleset shows **Active**, targets the default branch, lists **Repository admin** under bypass with **For pull requests only**, and requires the status checks "AI-assist declaration" and "AI-assist human review" from GitHub Actions.
5. Confirm it works: a direct push to `main` must fail with a "Repository rule violations found" error.

## Related repository settings

These are not part of the ruleset; set them once by hand.

- **Settings → Security → Private vulnerability reporting:** enable it, so [SECURITY.md](../SECURITY.md) reports have somewhere to go.
- **Settings → General → Pull Requests:** allow squash merging only, and turn on **Automatically delete head branches**.
