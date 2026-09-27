# AI-assisted development

The build plan requires AI-assisted changes to follow DoWI 8430.01 §3.6: every such change is labeled and reviewed by a human. This page records how the repository applies that requirement (ICS-003).

## Rules

1. **Declare.** Every pull request answers the "AI assistance" question in the [pull-request template](../.github/pull_request_template.md) by ticking exactly one box: **No AI assistance** or **AI-assisted**.
2. **Describe.** An AI-assisted pull request fills in three fields:
   - **Tool:** the product used, which must be in the register below.
   - **Model and version:** the model name and version the tool used for this change.
   - **Scope:** what the tool wrote or changed, and what a person wrote.
3. **Review.** A person is accountable for every AI-assisted change. The author reviews it before asking for review, and at least two people other than the author approve it before merge. Approvals from bots do not count.
4. **Register.** Only tools in the register may be used. Add or change a tool through a pull request that updates this page.
5. **Protect data.** Prompts, files and context given to an AI tool follow [SECURITY.md](../SECURITY.md): no CUI, CTI, export-controlled, classified, proprietary or credential material.

## Enforcement

Both checks are required status checks in the "Protect main" ruleset (see [repository-protection.md](repository-protection.md)). They run on every pull request and re-run when its description changes or a review is submitted.

| Check | Fails when | Also |
|---|---|---|
| AI-assist declaration | Neither box or both boxes are ticked, or an AI-assisted pull request leaves Tool, Model and version, or Scope empty | Adds or removes the `ai-assisted` label to match the answer |
| AI-assist human review | An AI-assisted pull request has fewer than two approvals from people other than its author | Skipped, and so passing, when the pull request is not AI-assisted |

While the repository has one person, the human-review check stays red on AI-assisted pull requests, and the owner merges them with the admin bypass. The red check is the record that the second approvals are missing.

## Log

The per-change log is the set of AI-assisted pull requests. [Filter pull requests by the `ai-assisted` label](https://github.com/MatthewK84/ICS/pulls?q=is%3Apr+label%3Aai-assisted) to list each change with its tool, model and version, and scope. Pull requests #98, #99 and #100, merged before these checks existed, were AI-assisted with Claude Code and say so in their descriptions.

## Approved tools

| Tool | Vendor | Approved scope | Added |
|---|---|---|---|
| Claude Code | Anthropic | Code, tests, documentation and repository automation, delivered as pull requests for human review | 2026-09-27 |

The model and version are recorded on each pull request rather than here, because they change over time.
