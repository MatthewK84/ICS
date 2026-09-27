# AI-assisted development

The build plan requires AI-assisted changes to follow DoWI 8430.01 §3.6: every such change is labeled and reviewed by a human. This page records how the repository applies that requirement (ICS-003).

## Rules

1. **Declare.** Every pull request answers the "AI assistance" question in the [pull-request template](../.github/pull_request_template.md) by ticking exactly one box: **No AI assistance** or **AI-assisted**.
2. **Describe.** An AI-assisted pull request fills in these fields:
   - **Tool** (required): the product used, which must be in the register below.
   - **Model and version** (optional): the model name and version the tool used for this change, when known.
   - **Scope** (required): what the tool wrote or changed, and what a person wrote.
3. **Review.** A person is accountable for every AI-assisted change. The owner reads every AI-assisted change before merging it. Approvals from bots do not count as that review.
4. **Register.** Only tools in the register may be used. Add or change a tool through a pull request that updates this page.
5. **Protect data.** Prompts, files and context given to an AI tool follow [SECURITY.md](../SECURITY.md): no CUI, CTI, export-controlled, classified, proprietary or credential material.

## Enforcement

The check below is a required status check in the "Protect main" ruleset (see [repository-protection.md](repository-protection.md)). It runs on every pull request and re-runs when the description changes or new commits are pushed.

| Check | Fails when | Also |
|---|---|---|
| AI-assist declaration | Neither box or both boxes are ticked, or an AI-assisted pull request leaves Tool or Scope empty | Adds or removes the `ai-assisted` label to match the answer |

Rule 3 is not a status check. The owner decided to drop the automated count of approvals from people other than the author, because the repository has one person and that check could never pass. The owner's own reading of each change before merge is the human review. Revisit this when other reviewers join; the pull-request rule in the ruleset still asks for two approvals, which the owner bypasses at merge.

## Log

The per-change log is the set of AI-assisted pull requests. [Filter pull requests by the `ai-assisted` label](https://github.com/MatthewK84/ICS/pulls?q=is%3Apr+label%3Aai-assisted) to list each change with its tool, scope and, where given, model and version. Pull requests #98, #99 and #100, merged before these checks existed, were AI-assisted with Claude Code and say so in their descriptions.

## Approved tools

| Tool | Vendor | Approved scope | Added |
|---|---|---|---|
| Claude Code | Anthropic | Code, tests, documentation and repository automation, delivered as pull requests for human review | 2026-09-27 |

The model and version are recorded on each pull request rather than here, because they change over time.
