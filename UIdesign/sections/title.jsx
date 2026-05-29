// title.jsx — S0: brief approach. Refinement-pass principles.
const TitleArtboard = () => {
  const t = window.tToken;
  return (
    <Frame kicker="toasty / observability runtime" title="lo-fi wireframes — refinement pass"
      subtitle="2026-05-28 · five surfaces · the telemetry semantics are still evolving"
      footer="reference vibe: Wireshark, Process Explorer, GPUView, WPA, VTune-era systems tooling. not Linear, Datadog, Grafana, gaming overlays.">
      <div style={{ display: "grid", gridTemplateColumns: "1.1fr 1fr", gap: 16, height: "100%" }}>
        <Panel title="design intent">
          <div style={{ fontSize: 13, lineHeight: 1.55 }}>
            An instrument panel for a system whose telemetry is partial, late,<br/>
            and sometimes wrong. The UI's job is to make that condition <i>legible</i>,<br/>
            not to hide it behind a clean number.
          </div>
          <H style={{ marginTop: 14 }}>priorities</H>
          <ol style={{ paddingLeft: 18, margin: "2px 0 0 0", fontSize: 12, lineHeight: 1.7 }}>
            <li>operational usefulness</li>
            <li>information hierarchy</li>
            <li>workflow clarity</li>
            <li>low cognitive load</li>
            <li>long-duration usability</li>
            <li>visual polish — last</li>
          </ol>

          <H style={{ marginTop: 14 }}>house rules (refined)</H>
          <ul style={{ paddingLeft: 18, margin: "2px 0 0 0", fontSize: 11.5, color: t.ink2, lineHeight: 1.65 }}>
            <li>every value carries its source(s)</li>
            <li>confidence is loud only when degraded · quiet by default</li>
            <li>disagreement is a state, not an error</li>
            <li>canonical source is always marked, with rationale</li>
            <li><b>fake precision is avoided</b> — values carry a qualifier</li>
            <li>"what changed since baseline" must be readable at a glance</li>
            <li>cost ≠ authority — they're separate dimensions</li>
            <li>the live view is the replay view at T = now (same renderer)</li>
          </ul>

          <H style={{ marginTop: 14 }}>value qualifiers</H>
          <div style={{ display: "grid", gridTemplateColumns: "120px 1fr", rowGap: 4, fontSize: 11 }}>
            <span><Val>18.9</Val></span><span style={{ color: t.ink3 }}>canonical</span>
            <span><Val kind="disputed">18.9</Val></span><span style={{ color: t.ink3 }}>disputed (sources disagree)</span>
            <span><Val kind="estimated">2300</Val></span><span style={{ color: t.ink3 }}>estimated · prefixed with ~</span>
            <span><Val kind="sampled" n={12}>2300</Val></span><span style={{ color: t.ink3 }}>sampled · count visible</span>
            <span><Val kind="stale" age="8s">2280</Val></span><span style={{ color: t.ink3 }}>stale · last good age</span>
            <span><Val kind="unsupported"/></span><span style={{ color: t.ink3 }}>unsupported on this build / driver</span>
          </div>
        </Panel>

        <Panel title="five surfaces" hint="this deck →">
          <div style={{ display: "flex", flexDirection: "column", gap: 8 }}>
            {[
              ["S1", "Session view",        "shell + adapters + arbitration + timeline. the primary live screen."],
              ["S2", "Adapter detail",      "reconciled identity grouped by origin · memory · activity · state."],
              ["S3", "Replay & comparison", "scrub a recording; overlay a second with a shared cursor."],
              ["S4", "Cost of observation", "self-metrics, per-collector cost, perturbation events. authority is a separate axis."],
              ["S0", "Approach (this card)", "principles, not a design system."],
            ].map(([id, name, desc]) => (
              <div key={id} style={{
                display: "grid", gridTemplateColumns: "40px 140px 1fr", gap: 10,
                alignItems: "baseline", padding: "4px 0",
                borderBottom: `1px dotted ${t.ink4}`,
              }}>
                <div style={{ fontSize: 11, color: t.ink3, letterSpacing: 1 }}>{id}</div>
                <div style={{ fontSize: 12, fontWeight: 600 }}>{name}</div>
                <div style={{ fontSize: 11, color: t.ink3, lineHeight: 1.45 }}>{desc}</div>
              </div>
            ))}
          </div>

          <H style={{ marginTop: 14 }}>color discipline</H>
          <div style={{ fontSize: 11, color: t.ink2, lineHeight: 1.55, marginBottom: 6 }}>
            Color encodes <i>collector state</i> only — never identity, never decoration.
            Stable / agreed values stay ink-on-paper.
          </div>
          <div style={{ display: "flex", gap: 8, flexWrap: "wrap" }}>
            <Chip variant="ok">agree / ok</Chip>
            <Chip variant="warn">disagree / fallback</Chip>
            <Chip variant="err">unsupported / stalled</Chip>
            <Chip variant="info">canonical pick</Chip>
            <Chip variant="muted">passive / steady</Chip>
          </div>

          <H style={{ marginTop: 14 }}>this pass — what changed</H>
          <ul style={{ paddingLeft: 18, margin: "2px 0 0 0", fontSize: 11, color: t.ink2, lineHeight: 1.55 }}>
            <li>removed redundant framing in S1 / S2 / S4</li>
            <li>baseline ghosting on sparklines &amp; timeline lanes</li>
            <li>canonical pick gets a <b>▸</b> bracket and rationale</li>
            <li>confidence bars only render when conf &lt; 0.9</li>
            <li>cost vs authority surfaced as two distinct concerns in S4</li>
            <li>replay reserves rail for future affordances (no premature design)</li>
          </ul>
        </Panel>
      </div>
    </Frame>
  );
};

window.TitleArtboard = TitleArtboard;
