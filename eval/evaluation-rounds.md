# Evaluation Rounds

## Benchmark Structure

The benchmark consists of:
- 5 core prompts
- Repeated across 3 rounds
- Total: 15 evaluated responses per model

Between rounds:
- The model may review its own prior responses
- The model may self-correct
- No external hints are provided

This measures:
- Reflection capability
- Error correction capability
- Self-improvement under iteration
- Persistence of hallucination
- Planning stabilization

---

## Round 1 — Cold Read

The model receives:
- Repository only
- No prior outputs

Measures:
- Raw repository comprehension

---

## Round 2 — Self-Reflection

The model receives:
- Repository
- Its own Round 1 responses

Measures:
- Error correction capability
- Stability improvement
- Reflection quality

---

## Round 3 — Stabilization

The model receives:
- Repository
- All prior responses

Measures:
- Convergence quality
- Hallucination reduction
- Final systems model maturity

---

## Execution Model (Revision 1)

**Stateless execution with explicit prior-round transcript injection.**

Interactive server state (persistent KV cache across rounds) is NOT used in Revision 1. Each of the 15 round-prompt invocations is a fresh `llama-cli` process that receives a complete, self-contained context.

Reasoning:
- Reproducibility, auditability, comparability, deterministic replay, and telemetry correlation all favor stateless execution.
- Interactive server state introduces hidden KV continuity, state contamination risk, tokenizer/session drift, cache persistence ambiguity, replay instability, and implementation-specific behavior variance — all problematic across multi-model / multi-quant / multi-backend matrices.
- Stateless execution keeps every round inspectable, every prompt reconstructable, every inference artifact reproducible, every scoring dispute auditable.
- The benchmark target is already in the 12K–16K token range; rounds are only 3 deep; prior outputs are themselves meaningful benchmark artifacts that we want explicit in the context.

Per-round context construction:

- **Round 1, Prompt P:** `<snapshot> + <Prompt P from prompt-sequence.md>`
- **Round 2, Prompt P:** `<snapshot> + <Round 1 response to Prompt P> + <Round 2 instruction> + <Prompt P>`
- **Round 3, Prompt P:** `<snapshot> + <Round 1 response to Prompt P> + <Round 2 response to Prompt P> + <Round 3 instruction> + <Prompt P>`

For Revision 1, each prompt's round-N invocation sees only its own per-prompt prior history (not cross-prompt). This bounds the context size and tests per-prompt self-correction cleanly. A later revision may explore cross-prompt awareness within rounds.

The full per-invocation prompt prefix is saved to `eval/runs/<config>/round-N/prompt-M-injected-context.md` alongside the response in `prompt-M.md`. This makes the reflection substrate fully inspectable: an evaluator can see exactly what prior reasoning the model received, and hallucination-persistence + contradiction-propagation become measurable rather than opaque.

Fast follow (post-Revision 1): add an interactive-server variant once we have a stateless baseline per model.
