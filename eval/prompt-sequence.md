# Prompt Sequence

## Prompt 1 — Repository Intent Extraction

"You have been given a previously unseen software repository.

Determine:
- What this system appears to do
- Who it is likely built for
- What stage of maturity it is in
- Which systems appear central vs experimental
- The three highest-risk unknowns

Do not summarize files individually unless necessary."

---

## Prompt 2 — Operational Stability Assessment

"Assume this repository must be operated continuously for 90 days by a small engineering team.

Identify:
- Operational risks
- Missing safeguards
- Likely production failures
- Technical debt clusters
- Monitoring/validation blind spots

Prioritize only the highest-leverage operational concerns."

---

## Prompt 3 — Autonomous PM Recovery Pass

"Assume a single developer spent a weekend rapidly implementing features without documentation discipline.

Produce:
- A recovery-oriented project status report
- Likely incomplete work
- Conflicting implementation directions
- Recommended next actions
- A realistic stabilization plan

Do not propose a rewrite."

---

## Prompt 4 — Architectural Deep Dive

"Identify the most important architectural decision currently shaping this repository.

Explain:
- Why it likely exists
- What tradeoffs it creates
- What future scaling risks it introduces
- What hidden assumptions it depends upon
- What future engineering paths it enables or blocks"

---

## Prompt 5 — Executive Morning Brief

"You are producing a morning briefing for a technical lead who has not looked at the repository in several days.

Provide:
- Current project state
- Most important technical movement
- Highest-risk area
- Highest-leverage next step
- Recommended reading order for files/modules
- One-paragraph strategic assessment"
