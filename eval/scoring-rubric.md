# Scoring Rubric

## Scoring Overview

| Category | Weight |
|---|---|
| Systems Comprehension | 20% |
| Operational Judgment | 20% |
| Architectural Reasoning | 20% |
| Prioritization Quality | 15% |
| Documentation Reconciliation | 10% |
| Self-Correction Across Rounds | 10% |
| Evidence Grounding | 5% |

Total possible score: 100 points

Documentation Reconciliation was added in Revision 1 as one of the benchmark's strongest differentiators from standard SWE evaluations. It measures the specific skill of using documented intent and observed implementation reality *against* each other — exactly the operational reasoning the benchmark is built to surface. Systems Comprehension and Evidence Grounding were reduced to make room; Evidence Grounding's narrowed weight reflects that its prior scope (citing concrete evidence) is now partly captured by Documentation Reconciliation (cross-validating evidence sources).

---

## 1. Systems Comprehension (25%)

Measures:
- Ability to infer repository purpose
- Correct identification of major subsystems
- Recognition of architecture patterns
- Understanding of dependency relationships
- Ability to distinguish core systems from peripheral systems

Penalty conditions:
- Superficial summaries
- Generic statements
- Misidentification of repository purpose

---

## 2. Operational Judgment (20%)

Measures:
- Real-world engineering practicality
- Ability to identify delivery blockers
- Recognition of unstable implementation patterns
- Detection of operational risk
- Ability to identify missing validation/testing paths

Penalty conditions:
- Excessive focus on formatting/style
- Missing critical operational concerns

---

## 3. Architectural Reasoning (20%)

Measures:
- Structural insight
- Long-term maintainability analysis
- Ability to infer architectural intent
- Detection of abstraction leakage
- Recognition of scaling bottlenecks

Penalty conditions:
- Purely tactical observations
- No higher-level synthesis

---

## 4. Prioritization Quality (15%)

Measures:
- Ability to rank work effectively
- Sequencing realism
- Distinguishing urgent vs important
- Understanding dependency ordering

Penalty conditions:
- Unrealistic rewrites
- Large-scale unnecessary refactors

---

## 5. Documentation Reconciliation (10%)

Measures:
- Whether the model cross-validates documented intent against implementation reality
- Whether it notices stale operational guidance
- Whether it distinguishes documented architecture from actual architecture
- Whether it treats documents as evidence rather than ground truth
- Whether it identifies contradictions between docs and code without over-trusting either

Penalty conditions:
- Treating docs as authoritative without verification
- Ignoring docs that exist in the repository
- Failing to flag obvious doc-vs-implementation contradictions
- Hallucinating documentation that does not exist

This is the benchmark's strongest differentiator from standard SWE evaluations. A capable autonomous analyst uses every artifact in the repository — including operator-facing docs, runbooks, and agent contracts — while remaining skeptical of any single source.

---

## 6. Evidence Grounding (5%)

Measures:
- Citation of concrete repository evidence
- Specificity
- Traceability of claims

Penalty conditions:
- Hallucinated systems
- Unsupported assertions

---

## 7. Self-Correction Across Rounds (10%)

Measures:
- Ability to revise conclusions
- Reduction of hallucinations
- Improvement of prioritization
- Stability of architectural understanding

Penalty conditions:
- Persistent hallucinations
- Contradictory reasoning
- Increasing instability over rounds

---

## Final Evaluation Rubric

| Score Range | Interpretation |
|---|---|
| 90–100 | Exceptional systems intelligence |
| 80–89 | Strong operational reasoning |
| 70–79 | Competent repository comprehension |
| 60–69 | Partial understanding with notable gaps |
| 50–59 | Superficial reasoning |
| <50 | Unreliable autonomous analysis |
