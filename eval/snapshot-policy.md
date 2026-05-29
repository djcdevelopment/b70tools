# Snapshot Policy

This document defines what goes into a benchmark snapshot of a target repository and what does not. The policy is informed by the principle that the benchmark models a real autonomous technical analyst entering a repository under operational conditions.

## Include

A snapshot SHOULD include:

- **Full file tree** (relative paths only; one path per line). Models read filenames as strong signals of intent and dead-end attempts.
- **README** at the repository root.
- **AGENTS.md** — the agent contract for the repo, if present. A real analyst reads this; the benchmark should too.
- **PROJECT_CONTEXT.md** and other architecture/intent docs the operator writes for collaborators.
- **Deployment runbooks** and **operational recipes** (e.g., `arc-b70-dual-70b-windows-vulkan.md`). These document working configurations and are exactly the artifacts a PM analyst would consult.
- **Top-level execution scripts** (e.g., `bench-*.ps1`).
- **Operational logs and their error counterparts** — including empty / 0-byte log files, which are themselves diagnostic artifacts.
- **`.claude/` or other agent-trace directories** when present in the target — they represent operational reality, not editorial commentary.
- **A curated subset of code files**: entry points, schema definitions, configuration files, and one representative file from each major subdirectory.

## Exclude

A snapshot MUST NOT include:

- **Evaluator annotations** — anything written specifically to grade or judge the model's eventual responses.
- **Benchmark hints** — pre-digested summaries or hints written for the benchmark itself.
- **Hidden repo summaries** crafted to give the model an unfair advantage on these specific prompts.
- **"Ground truth" packets** containing the expected answers or scoring criteria.
- **Implementation commentary written specifically for this benchmark** (as distinct from operator-facing docs the model would encounter in the wild).

## Exclude by default but inspectable

A snapshot SHOULD typically not include but MAY include with explicit rationale:

- **Binary build outputs** (DLLs, EXEs, .obj files). Always elide; record the paths-only count in the manifest.
- **Vendor / third-party bundled binaries** (e.g., `llamacpp-win-vulkan/` containing dropped-in vendor binaries). Elide contents; preserve tree presence.
- **Model files** (`.gguf`, `.safetensors`, `.onnx`, weights). Always elide.
- **Massive log files** (>1 MB). Truncate to head + tail with a `[N lines elided]` marker.
- **Generated artifacts** (`build/`, `dist/`, `out/`). Elide contents; preserve tree presence.

## Manifest

Every snapshot MUST emit a manifest documenting:

- Snapshot timestamp and target directory
- Token budget used and approximate utilization
- Files included verbatim (path + size)
- Files truncated (path + original size + truncation rule applied)
- Files elided entirely (path + reason)
- Directories whose contents were elided (path + file count elided + reason)

The manifest is the reproducibility contract. A snapshot's manifest must allow regenerating an equivalent snapshot deterministically.

## Reasoning

The benchmark should reward models that:
- read the docs that are present
- question those docs when implementation disagrees
- avoid over-trusting narrative artifacts
- distinguish documented intent from observed reality

That requires giving the model the same evidence surface a human analyst would actually have. Strict cold-blindfolded reading sounds rigorous but in practice tests something less useful: the model's ability to invent plausible architecture from filenames alone. That is not the operational skill the benchmark exists to measure.
