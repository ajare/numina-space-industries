# Domain docs

This repository uses a single domain context:
root `CONTEXT.md` and `docs/adr/`.

## Before exploring

Read root `CONTEXT.md` and ADRs relevant to the work.

If `CONTEXT-MAP.md` exists, follow it to the relevant context
glossaries and context-scoped ADRs instead.

If domain documents are absent, proceed silently. Do not
suggest creating them upfront; domain-modeling creates them
lazily as terms and decisions are resolved.

## Vocabulary

Use the glossary's canonical terms in issues, proposals,
hypotheses, tests, and code. Avoid explicitly discouraged
synonyms.

If a needed concept is missing, reconsider the terminology
or note the gap for domain-modeling.

## Decision conflicts

Explicitly flag proposals that contradict an existing ADR,
identifying the ADR and explaining why it should be reopened.
