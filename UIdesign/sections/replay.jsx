// replay.jsx — S3: replay & comparison. Light. Same renderer as live view.
// refinement pass: reduce framing, surface future affordances as labelled
// placeholders (jump-to-disagreement, bookmarks, what-diverged?, annotations),
// keep the synchronized-cursor model dominant.
const ReplayArtboard = () => {
  const t = window.tToken;
  return (
    <Frame kicker="S3 · replay & comparison" title="overlay: vk-split-32b-q4  ↔  vk-split-32b-q4-pre-driver"
      subtitle="single recording = scrub. two recordings = scrub with shared cursor, aligned at workload-begin."
      footer="the replay view ≡ the live view at T = T_cursor. nothing rendered is computed from data not in the .jsonl.">
      <div style={{ border: `1px solid ${t.ink}`, background: t.paper,
                    display: "grid",
                    gridTemplateRows: "auto auto auto 1fr auto",
                    height: "100%" }}>

        {/* Recording headers (A · B) */}
        <div style={{ padding: "10px 16px", borderBottom: `1px solid ${t.ink3}`,
                      display: "grid", gridTemplateColumns: "1fr 1fr auto", gap: 16, alignItems: "center" }}>
          <RecordingHeader idx="A" name="vk-split-32b-q4"             date="2026-05-28 14:02 → 14:32"
            build="0.4.2-a93f · vk 1.3.290 · drv 32.0.101.5972" canonical/>
          <RecordingHeader idx="B" name="vk-split-32b-q4-pre-driver"  date="2026-05-25 11:08 → 11:38"
            build="0.4.2-a93f · vk 1.3.290 · drv 32.0.101.5689"/>
          <div style={{ display: "flex", gap: 6 }}>
            <Btn>swap A↔B</Btn>
            <Btn>+ overlay</Btn>
            <Btn>export clip</Btn>
          </div>
        </div>

        {/* Fingerprint diff — thin strip */}
        <div style={{ padding: "5px 16px", borderBottom: `1px solid ${t.ink4}`, fontSize: 11,
                      display: "flex", gap: 16, flexWrap: "wrap", color: t.ink2 }}>
          <span style={{ color: t.ink3 }}>fingerprint diff:</span>
          <span><b>driver</b> 5689 → <b>5972</b> <Chip variant="warn" style={{ marginLeft: 4 }}>changed</Chip></span>
          <span><b>vk</b> 1.3.290 <span style={{ color: t.ink4 }}>=</span></span>
          <span><b>ReBAR</b> on <span style={{ color: t.ink4 }}>=</span></span>
          <span><b>BIOS</b> 1.3.4 <span style={{ color: t.ink4 }}>=</span></span>
          <span><b>collectors</b> 4 / 4 · <span style={{ color: t.ink3 }}>igcl participation differs</span></span>
        </div>

        {/* Tabs */}
        <div style={{ borderBottom: `1px solid ${t.ink4}` }}>
          <Tabs items={["overlay", "compare at cursor", "disagreement diff", "events", "annotations"]} active={0}
                style={{ padding: "0 16px" }}/>
        </div>

        {/* Body: overlay + side-by-side compare + placeholder rail */}
        <div style={{ display: "grid",
                      gridTemplateColumns: "1.5fr 1fr 200px",
                      height: "100%", minHeight: 0 }}>
          <div style={{ borderRight: `1px solid ${t.ink4}`, padding: "12px 16px",
                        display: "flex", flexDirection: "column", gap: 8 }}>
            <OverlayLanes/>
          </div>
          <div style={{ borderRight: `1px solid ${t.ink4}`, padding: "12px 16px",
                        display: "flex", flexDirection: "column", gap: 8 }}>
            <H><span>values at cursor</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                T = 14:14:08 (A) · 11:20:08 (B) · aligned at workload-begin + 6m
              </span>
            </H>
            <CompareTable/>
          </div>
          <div style={{ padding: "12px 12px", display: "flex", flexDirection: "column", gap: 12 }}>
            <FutureRail/>
          </div>
        </div>

        {/* Scrub bar — full width */}
        <ScrubBar/>
      </div>
    </Frame>
  );
};

function RecordingHeader({ idx, name, date, build, canonical }) {
  const t = window.tToken;
  return (
    <div style={{ display: "flex", gap: 10, alignItems: "center" }}>
      <span style={{
        width: 26, height: 26, border: `1px solid ${t.ink}`,
        display: "inline-flex", alignItems: "center", justifyContent: "center",
        fontWeight: 700, background: idx === "A" ? t.ink : t.paper,
        color: idx === "A" ? t.paper : t.ink, fontSize: 14,
      }}>{idx}</span>
      <div>
        <div style={{ fontWeight: 600, fontSize: 13 }}>{name}</div>
        <div style={{ color: t.ink3, fontSize: 10 }}>{date}</div>
        <div style={{ color: t.ink3, fontSize: 10 }}>{build}</div>
      </div>
    </div>
  );
}

function OverlayLanes() {
  const t = window.tToken;
  return (
    <>
      <H><span>overlay timeline</span>
        <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
          solid = A · dashed = B · aligned at workload-begin · baselines ghosted
        </span>
      </H>
      <svg width="100%" height="320" viewBox="0 0 1100 320" preserveAspectRatio="none">
        {[
          ["vram.A0",  30],
          ["vram.A1", 105],
          ["clk.A0",  180],
          ["clk.A1",  255],
        ].map(([l, y]) => (
          <text key={l} x="4" y={y + 4} fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>{l}</text>
        ))}
        {/* baseline regions (shared) */}
        <rect x="70" y="0" width="180" height="280" fill={t.paper2}/>
        <text x="78" y="12" fontFamily={t.fontMono} fontSize="9" fill={t.ink3} letterSpacing="1">BASELINE</text>

        {/* per-lane baseline ghost lines */}
        {[46, 121, 196, 271].map((y, i) => (
          <line key={i} x1="0" x2="1100" y1={y} y2={y} stroke={t.ink4} strokeDasharray="2 4" strokeWidth="0.6"/>
        ))}

        {/* A traces (solid) */}
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="70,46 250,46 430,30 610,22 790,22 970,22 1070,22"/>
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="70,121 250,121 430,98 610,86 790,82 970,82 1070,82"/>
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="70,196 250,196 430,180 610,172 790,168 970,168 1070,168"/>
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="70,271 250,271 430,250 610,242 790,240 970,240 1070,240"/>

        {/* B traces (dashed) */}
        <polyline fill="none" stroke={t.ink2} strokeWidth="1.3" strokeDasharray="5 3"
          points="70,52 250,52 430,38 610,30 790,28 970,28 1070,28"/>
        <polyline fill="none" stroke={t.ink2} strokeWidth="1.3" strokeDasharray="5 3"
          points="70,128 250,128 430,118 610,108 790,104 970,104 1070,104"/>
        <polyline fill="none" stroke={t.ink2} strokeWidth="1.3" strokeDasharray="5 3"
          points="70,200 250,200 430,178 610,168 790,164 970,164 1070,164"/>
        <polyline fill="none" stroke={t.ink2} strokeWidth="1.3" strokeDasharray="5 3"
          points="70,272 250,272 430,244 610,232 790,228 970,228 1070,228"/>

        {/* disagreement band on A1 in A only */}
        <rect x="700" y="78" width="370" height="10" fill="none" stroke={t.accentY} strokeDasharray="3 3"/>
        <text x="700" y="76" fontFamily={t.fontMono} fontSize="9" fill={t.accentY}>A: vk≠dxgi (B: no disagreement)</text>

        {/* events */}
        {[
          [70,  t.ink,    "open"],
          [250, t.accentG,"baseline end"],
          [430, t.accentY,"workload begin"],
          [700, t.accentY,"A disagree A1"],
          [970, t.accentR,"A igcl stall"],
        ].map((e, i) => (
          <g key={i}>
            <line x1={e[0]} x2={e[0]} y1="0" y2="290" stroke={e[1]} strokeWidth="0.6" strokeDasharray="2 3"/>
            <rect x={e[0] - 3} y="290" width="6" height="6" fill={e[1]}/>
            <text x={e[0] + 8} y="298" fontFamily={t.fontMono} fontSize="9" fill={t.ink2}>{e[2]}</text>
          </g>
        ))}

        {/* cursor */}
        <line x1="970" x2="970" y1="0" y2="320" stroke={t.ink} strokeWidth="1.6"/>
        <g transform="translate(976, 4)">
          <rect width="120" height="42" fill={t.paper} stroke={t.ink}/>
          <text x="6" y="13" fontFamily={t.fontMono} fontSize="10" fill={t.ink}>cursor · +6m</text>
          <text x="6" y="26" fontFamily={t.fontMono} fontSize="9" fill={t.ink2}>A: 14:14:08</text>
          <text x="6" y="38" fontFamily={t.fontMono} fontSize="9" fill={t.ink2}>B: 11:20:08</text>
        </g>
      </svg>
    </>
  );
}

function CompareTable() {
  const t = window.tToken;
  const rows = [
    ["vram.A0 resident",  "21.4 GiB", "20.8 GiB", "+0.6", "muted", "canonical"],
    ["vram.A1 resident",  "18.9 GiB", "19.4 GiB", "−0.5", "muted", "disputed"],
    ["vram.A1 budget",    "30.4 GiB", "31.1 GiB", "−0.7", "muted", "canonical"],
    ["clk.eng.A0",        "2300",     "2280",     "+20",  "muted", "estimated"],
    ["clk.eng.A1",        "2300",     "2300",     "0",    "muted", "estimated"],
    ["clk.mem.A1",        "—",        "1850",     "n/a",  "err",   "unsupported"],
    ["adapter wake A0",   "D0",       "D0",       "=",    "muted", "canonical"],
    ["adapter wake A1",   "D0",       "D0",       "=",    "muted", "canonical"],
    ["disagree count",    "2",        "0",        "+2",   "warn",  "canonical"],
    ["igcl participation","stalled",  "ok",       "↓",    "warn",  "canonical"],
  ];
  const kindFor = (k) => k;
  return (
    <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 11, marginTop: 4 }}>
      <thead>
        <tr style={{ color: t.ink3, fontSize: 9, textTransform: "uppercase", letterSpacing: 1 }}>
          <th style={th2(t)}>metric</th>
          <th style={th2(t)}>A</th>
          <th style={th2(t)}>B</th>
          <th style={{ ...th2(t), textAlign: "right" }}>Δ A−B</th>
        </tr>
      </thead>
      <tbody>
        {rows.map((r, i) => (
          <tr key={i} style={{ borderBottom: `1px dotted ${t.ink4}` }}>
            <td style={td2(t, t.ink3)}>
              {r[0]}
              {r[5] !== "canonical" && (
                <span style={{ fontSize: 9, color: t.ink4, marginLeft: 6, fontStyle: "italic" }}>{r[5]}</span>
              )}
            </td>
            <td style={td2(t)}>
              {r[5] === "unsupported" ? <Val kind="unsupported"/> : <Val kind={kindFor(r[5])}>{r[1]}</Val>}
            </td>
            <td style={td2(t, t.ink3)}>{r[5] === "unsupported" ? r[2] : r[2]}</td>
            <td style={{ ...td2(t), textAlign: "right", fontWeight: 600,
                         color: r[4] === "warn" ? t.accentY : r[4] === "err" ? t.accentR : t.ink }}>
              {r[3]}
            </td>
          </tr>
        ))}
      </tbody>
    </table>
  );
}

function FutureRail() {
  const t = window.tToken;
  const Item = ({ children, sub, label = "todo" }) => (
    <div style={{
      padding: "6px 8px", border: `1px dashed ${t.ink4}`,
      fontSize: 11, color: t.ink2, lineHeight: 1.4, position: "relative",
    }}>
      <div>{children}</div>
      {sub && <div style={{ fontSize: 9, color: t.ink3, marginTop: 2 }}>{sub}</div>}
      <span style={{
        position: "absolute", top: -7, right: 6, background: t.paper, padding: "0 4px",
        fontSize: 8, letterSpacing: 1, textTransform: "uppercase", color: t.ink4,
      }}>{label}</span>
    </div>
  );
  return (
    <>
      <H><span>navigation</span></H>
      <Item label="planned">▸ jump to first disagreement<div style={{ marginTop: 2 }}><Key>g</Key><Key>D</Key></div></Item>
      <Item label="planned">▸ jump to next adapter-state change<div style={{ marginTop: 2 }}><Key>g</Key><Key>S</Key></div></Item>
      <Item label="planned">▸ bookmarks (0)<div style={{ marginTop: 2 }}><Key>m</Key> mark · <Key>`</Key> jump</div></Item>

      <H style={{ marginTop: 6 }}><span>analysis</span></H>
      <Item label="placeholder" sub="what changed between A and B, ranked by deviation. produced from JSONL diff, not heuristics.">
        ▸ what diverged?<br/>
        <span style={{ color: t.ink3 }}>(auto-summary)</span>
      </Item>
      <Item label="placeholder" sub="annotate any T with a note; persisted in the JSONL.">
        ▸ annotations
      </Item>
      <Item label="placeholder" sub="apply a saved alignment / metric set to any pair of recordings.">
        ▸ comparison presets
      </Item>

      <div style={{ fontSize: 9, color: t.ink4, lineHeight: 1.45, marginTop: 4 }}>
        these are scaffolds, not finished UI. the layout reserves room for them so the architecture isn't backed into a corner once the operational patterns settle.
      </div>
    </>
  );
}

function ScrubBar() {
  const t = window.tToken;
  return (
    <div style={{ padding: "10px 16px", borderTop: `1px solid ${t.ink3}` }}>
      <div style={{ display: "flex", alignItems: "center", gap: 14, fontSize: 11 }}>
        <Btn>⏮</Btn><Btn>⏪</Btn><Btn>⏵</Btn><Btn>⏩</Btn><Btn>⏭</Btn>
        <span style={{ color: t.ink3 }}>1× speed · realign at <b>workload-begin</b></span>
        <span style={{ marginLeft: "auto", color: t.ink3 }}>
          A: 14:02 → 14:32 (30:00) · B: 11:08 → 11:38 (30:00)
        </span>
      </div>
      <div style={{ marginTop: 8, position: "relative" }}>
        <svg width="100%" height="38" viewBox="0 0 1500 38" preserveAspectRatio="none">
          <rect x="0" y="14" width="1500" height="10" fill={t.paper2} stroke={t.ink4}/>
          {/* baseline band */}
          <rect x="0" y="14" width="240" height="10" fill={t.paper3}/>
          {/* event ticks A (top half) */}
          {[240, 430, 700, 970].map((x, i) => (
            <line key={i} x1={x} x2={x} y1="10" y2="22" stroke={t.accentY} strokeWidth="1"/>
          ))}
          {/* event ticks B (bottom half) */}
          {[240, 430, 760].map((x, i) => (
            <line key={i} x1={x} x2={x} y1="22" y2="34" stroke={t.ink3} strokeWidth="1" strokeDasharray="2 2"/>
          ))}
          {/* cursor */}
          <line x1="970" x2="970" y1="6" y2="34" stroke={t.ink} strokeWidth="1.6"/>
          <polygon points="965,6 975,6 970,12" fill={t.ink}/>
          <text x="0" y="8" fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>BASELINE</text>
          <text x="430" y="8" fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>workload</text>
        </svg>
      </div>
      <div style={{ display: "flex", gap: 16, marginTop: 4, fontSize: 10, color: t.ink3 }}>
        <span><Key>space</Key> play/pause</span>
        <span><Key>←</Key>/<Key>→</Key> ±1 tick</span>
        <span><Key>⇧←</Key>/<Key>⇧→</Key> next event</span>
        <span><Key>[</Key>/<Key>]</Key> jump to disagreement</span>
        <span><Key>a</Key> realign mode</span>
        <span><Key>m</Key> bookmark</span>
      </div>
    </div>
  );
}

function th2(t) {
  return { textAlign: "left", padding: "4px 6px", borderBottom: `1px solid ${t.ink3}`, fontWeight: 600 };
}
function td2(t, color) {
  return { padding: "3px 6px", color: color || t.ink };
}

window.ReplayArtboard = ReplayArtboard;
