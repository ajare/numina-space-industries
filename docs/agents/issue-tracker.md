# Issue tracker: GitHub

Issues and specs live in GitHub Issues for
`ajare/numina-space-industries`. Use the `gh` CLI.

## Conventions

- Create: `gh issue create --title "..." --body "..."`.
  Use a heredoc for multiline bodies.
- Read: `gh issue view <number> --comments`.
  Fetch labels when relevant.
- List: `gh issue list --state open`, with appropriate filters.
- Comment: `gh issue comment <number> --body "..."`.
- Label: `gh issue edit <number> --add-label "..."` or
  `--remove-label "..."`.
- Close: `gh issue close <number> --comment "..."`.

Infer the repository from the Git remote.

## Pull requests as a triage surface

**PRs as a request surface: no.**

## Skill instructions

“Publish to the issue tracker” means create a GitHub issue.
“Fetch the relevant ticket” means read the issue and its comments.

## Wayfinding

- Keep the map in one issue labelled `wayfinder:map`, with
  Notes, Decisions-so-far, and Fog sections.
- Link child tickets using GitHub sub-issues. If unavailable,
  use a task list in the map and `Part of #<map>` in each child.
- Label children `wayfinder:<type>`: research, prototype,
  grilling, or task.
- Use native GitHub issue dependencies for blockers. If
  unavailable, record `Blocked by: #<number>` in the child.
- The frontier consists of open, unassigned children with no
  open blockers, in map order.
- Claim a ticket with `gh issue edit <number> --add-assignee @me`
  before working on it.
- Resolve by commenting with the outcome, closing the ticket,
  and adding a summary and link to the map's Decisions-so-far.
