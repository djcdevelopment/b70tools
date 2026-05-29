# Benchmark Overview

## What this benchmark measures (and what it does not)

This benchmark is **not** primarily testing coding, summarization, or static question-answering. It is testing:

- evidence synthesis under ambiguity
- operational reasoning
- uncertainty management
- architectural inference
- self-correction behavior
- systems intelligence

The central question is **not** "Can the model reverse-engineer a repository blindfolded?" It is:

> Can the model responsibly synthesize incomplete, partially stale, partially contradictory evidence into operationally useful understanding?

The benchmark therefore models a real autonomous technical analyst entering a repository under operational conditions — including the documents a competent human engineer would actually read. Withholding architecture notes, runbooks, and agent-facing operational docs would create an artificially adversarial environment that over-rewards code archaeology and under-measures operational reasoning. The doc-inclusion policy is specified separately in `snapshot-policy.md`.

## Objective

This benchmark evaluates the practical systems intelligence of local and frontier-capable LLMs under constrained conditions.

The evaluated model ("student") receives:
- A previously unseen software repository
- No internet access
- No human guidance
- No architectural briefing
- No documentation guarantees

The model must infer:
- System architecture
- Operational state
- Risk
- Intent
- Technical debt
- Priority actions
- Delivery strategy

## Core Evaluation Philosophy

This benchmark measures:
- Systems comprehension
- Multi-step synthesis
- Technical prioritization
- Architectural judgment
- Operational realism
- Recovery from incomplete information

The benchmark intentionally resembles real engineering leadership and autonomous software analysis work.

## Test Environment Constraints

Each evaluated model:
- Receives identical repository snapshots
- Receives identical prompts
- Operates with no external retrieval
- Has no internet access
- Must reason only from repository state

The repository should preferably contain:
- unfinished features
- TODOs
- dead code
- partially implemented systems
- mixed architectural intent
- stale documentation
- inconsistent naming

This benchmark intentionally rewards recovery from chaos.
