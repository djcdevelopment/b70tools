# prompts.ps1 — verbatim copy of the prompt sequence + round instructions from eval/prompt-sequence.md.
# Sourced by run-eval.ps1.

$PromptSequence = @(
    @{
        id = 1
        title = 'Repository Intent Extraction'
        text = @"
You have been given a previously unseen software repository.

Determine:
- What this system appears to do
- Who it is likely built for
- What stage of maturity it is in
- Which systems appear central vs experimental
- The three highest-risk unknowns

Do not summarize files individually unless necessary.
"@
    },
    @{
        id = 2
        title = 'Operational Stability Assessment'
        text = @"
Assume this repository must be operated continuously for 90 days by a small engineering team.

Identify:
- Operational risks
- Missing safeguards
- Likely production failures
- Technical debt clusters
- Monitoring/validation blind spots

Prioritize only the highest-leverage operational concerns.
"@
    },
    @{
        id = 3
        title = 'Autonomous PM Recovery Pass'
        text = @"
Assume a single developer spent a weekend rapidly implementing features without documentation discipline.

Produce:
- A recovery-oriented project status report
- Likely incomplete work
- Conflicting implementation directions
- Recommended next actions
- A realistic stabilization plan

Do not propose a rewrite.
"@
    },
    @{
        id = 4
        title = 'Architectural Deep Dive'
        text = @"
Identify the most important architectural decision currently shaping this repository.

Explain:
- Why it likely exists
- What tradeoffs it creates
- What future scaling risks it introduces
- What hidden assumptions it depends upon
- What future engineering paths it enables or blocks
"@
    },
    @{
        id = 5
        title = 'Executive Morning Brief'
        text = @"
You are producing a morning briefing for a technical lead who has not looked at the repository in several days.

Provide:
- Current project state
- Most important technical movement
- Highest-risk area
- Highest-leverage next step
- Recommended reading order for files/modules
- One-paragraph strategic assessment
"@
    }
)

# Per-round preface text injected before the prompt.
$RoundPreface = @{
    1 = ''
    2 = @"
You previously analyzed this repository in Round 1. Your prior response to this same prompt is provided below for your reference.

Review your prior reasoning. Where it remains correct, keep it. Where it was wrong, vague, or hallucinated, correct it explicitly and note what changed. Where new evidence in the repository contradicts your prior claims, acknowledge the contradiction directly. Your goal is improvement, not repetition.

If your prior response was substantially correct, a shorter Round 2 response that surgically updates the analysis is preferred over a full rewrite.
"@
    3 = @"
You previously analyzed this repository in Round 1 and revised your analysis in Round 2. Both prior responses to this same prompt are provided below.

This is the stabilization round. Produce your final analysis. Reconcile any contradictions between Round 1 and Round 2. Mark any claim where your confidence has changed across rounds. Persistent hallucinations or contradictions across all three rounds will count against the model's score; explicit acknowledgement and correction will count in its favor.

Aim for a final response that a technical lead could act on without further clarification.
"@
}
