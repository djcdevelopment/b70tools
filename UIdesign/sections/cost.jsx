// cost.jsx — S4: cost of observation. "did observing the system perturb the system?"
// refinement pass: separate cost from authority (two distinct dimensions),
// lighter grid for the capability matrix, qualifier-aware values.
const CostArtboard = () => {
  const t = window.tToken;
  return (
    <Frame kicker="S4 · cost of observation" title="how expensive is toasty right now?"
      subtitle="self-metrics · per-collector cost · authority is a separate dimension below"
      footer="cost ≠ authority. a cheap collector can be canonical; an expensive one may be advisory. these are graphed as two different concerns on purpose.">
      <div style={{ border: `1px solid ${t.ink}`, background: t.paper, padding: 0,
                    display: "grid",
                    gridTemplateColumns: "1fr 1fr",
                    gridTemplateRows: "auto auto 1fr",
                    height: "100%" }}>

        {/* TOP: self stat strip — full width */}
        <div style={{ gridColumn: "1 / -1", padding: "12px 16px", borderBottom: `1px solid ${t.ink3}` }}>
          <H><span>self</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              toasty.exe · pid 4812 · uptime 00:14:22 · stamped into the same JSONL
            </span>
          </H>
          <div style={{ display: "grid", gridTemplateColumns: "repeat(6, 1fr)", gap: 16, marginTop: 10 }}>
            <SelfStat label="cpu (5s avg)"     v={<Val>1.4</Val>} unit="%"
                      sub="cap 2.0 · within budget" trace={[0.9,1.1,1.3,1.2,1.4,1.5,1.4]} base={0.8}/>
            <SelfStat label="rss"              v={<Val>142</Val>} unit="MiB"
                      sub="cap 256 · stable"        trace={[120,128,132,138,140,141,142]} base={120}/>
            <SelfStat label="threads"          v={<Val>9</Val>}
                      sub="poll · arbitrate · jsonl · 6 collectors" trace={[9,9,9,9,9,9,9]} base={9}/>
            <SelfStat label="loaded modules"   v={<Val>47</Val>}
                      sub="Δ +0 since open"         trace={[47,47,47,47,47,47,47]} base={47}/>
            <SelfStat label="GPU allocations"  v={<Val>0</Val>}
                      sub="no VkDevice opened"      trace={[0,0,0,0,0,0,0]} base={0} good/>
            <SelfStat label="jsonl write rate" v={<Val kind="sampled" n={300}>2.1</Val>} unit="KiB/s"
                      sub="fsync every 5s · 4.2 MiB total" trace={[1.8,2.0,2.1,2.0,2.2,2.1,2.1]} base={2.0}/>
          </div>
        </div>

        {/* LEFT: per-collector cost ledger */}
        <div style={{ borderRight: `1px solid ${t.ink4}`, padding: "12px 16px" }}>
          <H><span>collector cost ledger</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              what each collector costs to run · class · last latency · participation
            </span>
          </H>
          <table style={{ width: "100%", borderCollapse: "collapse", fontSize: 11.5, marginTop: 6 }}>
            <thead>
              <tr style={{ color: t.ink3, fontSize: 9, textTransform: "uppercase", letterSpacing: 1 }}>
                <th style={th3(t)}>collector</th>
                <th style={th3(t)}>class</th>
                <th style={th3(t)}>last</th>
                <th style={th3(t)}>p50 / p99</th>
                <th style={th3(t)}>cost · 5m</th>
                <th style={{ ...th3(t), textAlign: "right" }}>state</th>
              </tr>
            </thead>
            <tbody>
              {[
                ["dxgi.queryAdapter",       "TrulyPassive",  "1 ms",   "1 / 3 ms",    "0.04% cpu",  "ok"],
                ["vk.memory.budget",        "TrulyPassive",  "32 ms",  "28 / 44 ms",  "0.61% cpu",  "ok"],
                ["setupapi.enumDevices",    "TrulyPassive",  "—",      "9 / 14 ms",   "0.02% cpu",  "ok"],
                ["vk.physicalDeviceProps",  "TrulyPassive",  "—",      "11 / 18 ms",  "0.01% cpu",  "ok"],
                ["igcl.metrics",            "DriverPassive", "210 ms", "44 / 280 ms", "0.71% cpu",  "warn"],
                ["self.process",            "TrulyPassive",  "<1 ms",  "<1 / 1 ms",   "0.02% cpu",  "ok"],
              ].map((r, i) => (
                <tr key={i} style={{ borderBottom: `1px dotted ${t.ink4}` }}>
                  <td style={td3(t)}>{r[0]}</td>
                  <td style={td3(t, r[1] === "DriverPassive" ? t.accentY : t.ink3)}>{r[1]}</td>
                  <td style={td3(t)}>{r[2]}</td>
                  <td style={td3(t, t.ink3)}>{r[3]}</td>
                  <td style={td3(t)}>{r[4]}</td>
                  <td style={{ ...td3(t), textAlign: "right" }}><Chip variant={r[5]}>{r[5]}</Chip></td>
                </tr>
              ))}
            </tbody>
          </table>
          <div style={{ fontSize: 10, color: t.ink3, marginTop: 10, lineHeight: 1.55 }}>
            <b>TrulyPassive</b> · pure user-mode read of OS / driver structures, no driver work scheduled.<br/>
            <b>DriverPassive</b> · initiates driver work that <i>could</i> perturb timing or state. flagged &amp; budgeted; never silent.
          </div>

          {/* perturbation events folded under cost */}
          <H style={{ marginTop: 16 }}>
            <span>perturbation events</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              measurably changed the system · linked back into the timeline
            </span>
          </H>
          <div style={{ fontSize: 11, marginTop: 4 }}>
            {[
              ["14:13:48", "igcl.metrics", "210 ms call · poll skipped a tick", "vram trace gap @ 14:13:48", "warn"],
              ["14:09:12", "igcl.metrics", "44 ms call (still passive class)",   "—",                          "muted"],
              ["14:02:13", "vk.enumerate*", "first call · 18 ms",                "one-time startup",           "muted"],
            ].map((r, i) => (
              <div key={i} style={{
                display: "grid", gridTemplateColumns: "70px 130px 1fr 1fr 50px",
                gap: 8, padding: "3px 0", borderTop: i === 0 ? "none" : `1px dotted ${t.ink4}`,
              }}>
                <span style={{ color: t.ink3 }}>{r[0]}</span>
                <span>{r[1]}</span>
                <span style={{ color: t.ink3 }}>{r[2]}</span>
                <span style={{ color: t.ink3, fontStyle: "italic" }}>{r[3]}</span>
                <span style={{ textAlign: "right" }}><Chip variant={r[4]}>{r[4]}</Chip></span>
              </div>
            ))}
          </div>
        </div>

        {/* RIGHT: authority matrix (separate dimension) */}
        <div style={{ padding: "12px 16px" }}>
          <H><span>authority matrix</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              who is canonical for which field, on this build · separate from cost
            </span>
          </H>
          <AuthorityMatrix/>

          <div style={{ fontSize: 10, color: t.ink3, marginTop: 12, lineHeight: 1.5 }}>
            <Dot color={t.ink}/> canonical here · <Dot color={t.accentY}/> degraded / falling back · <Dot color={t.ink4}/> advisory · ✕ unsupported.
            <br/>
            authority is decided per-field by the rule set in <b>settings → fallback rules</b>, not by per-collector reputation.
          </div>

          <H style={{ marginTop: 18 }}>
            <span>two-axis view</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              cost vs authority · where collectors sit today
            </span>
          </H>
          <TwoAxis/>
          <div style={{ fontSize: 10, color: t.ink3, marginTop: 8, lineHeight: 1.55 }}>
            cheap &amp; authoritative is a fine corner. cheap &amp; advisory is also fine — it cross-checks. expensive &amp; authoritative is what we audit hardest.
          </div>
        </div>
      </div>
    </Frame>
  );
};

function SelfStat({ label, v, unit, sub, trace, base, good }) {
  const t = window.tToken;
  const delta = trace[trace.length - 1] - base;
  return (
    <div>
      <div style={{ fontSize: 9, color: t.ink3, textTransform: "uppercase", letterSpacing: 0.8 }}>{label}</div>
      <div style={{ display: "flex", alignItems: "baseline", gap: 4, marginTop: 2 }}>
        <span style={{ fontSize: 22, fontWeight: 700 }}>{v}</span>
        {unit && <span style={{ fontSize: 11, color: t.ink3 }}>{unit}</span>}
      </div>
      <div style={{ fontSize: 10, color: good ? t.accentG : t.ink3 }}>{sub}</div>
      <div style={{ marginTop: 4 }}>
        <BaselineSpark trace={trace} base={base} w={180} h={20}/>
      </div>
      {Math.abs(delta) > 0.01 && (
        <div style={{ fontSize: 9, color: t.ink3, marginTop: 2 }}>
          Δ {delta > 0 ? "+" : "−"}{Math.abs(delta).toFixed(2)} vs baseline
        </div>
      )}
    </div>
  );
}

function BaselineSpark({ trace, base, w = 180, h = 20 }) {
  const t = window.tToken;
  const all = [...trace, base];
  const max = Math.max(...all), min = Math.min(...all, 0);
  const r = max - min || 1;
  const y = (val) => h - 2 - ((val - min) / r) * (h - 4);
  const pts = trace.map((d, i) => `${(i / Math.max(trace.length - 1, 1)) * w},${y(d)}`).join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block" }}>
      <line x1="0" y1={y(base)} x2={w} y2={y(base)} stroke={t.ink4} strokeDasharray="2 3" strokeWidth="0.6"/>
      <polyline points={pts} fill="none" stroke={t.ink2} strokeWidth="1.2"/>
    </svg>
  );
}

function AuthorityMatrix() {
  const t = window.tToken;
  const sources = ["vk", "dxgi", "igcl", "setupapi"];
  // mark: "C" canonical · "F" fallback/degraded · "A" advisory · "X" unsupported · "—" n/a
  const rows = [
    ["adapter identity",   "A","A","—","C"],
    ["vram resident",      "C","A","—","—"],
    ["vram budget",        "C","—","—","—"],
    ["engine clock",       "—","—","F","—"],
    ["memory clock",       "—","—","X","—"],
    ["adapter wake state", "—","C","—","—"],
    ["pci topology",       "A","A","—","C"],
    ["ReBAR state",        "C","A","—","—"],
  ];
  return (
    <div style={{ marginTop: 6 }}>
      <div style={{
        display: "grid", gridTemplateColumns: "1fr 50px 50px 50px 60px",
        fontSize: 9, color: t.ink3, textTransform: "uppercase", letterSpacing: 1,
        paddingBottom: 4, borderBottom: `1px solid ${t.ink4}`,
      }}>
        <span>field</span>
        {sources.map(s => <span key={s} style={{ textAlign: "center" }}><Src name={s}/></span>)}
      </div>
      {rows.map(([label, ...cells], i) => (
        <div key={i} style={{
          display: "grid", gridTemplateColumns: "1fr 50px 50px 50px 60px",
          fontSize: 11, padding: "4px 0",
          borderBottom: i === rows.length - 1 ? "none" : `1px dotted ${t.ink4}`,
        }}>
          <span style={{ color: t.ink3 }}>{label}</span>
          {cells.map((c, j) => (
            <span key={j} style={{ textAlign: "center" }}>
              <AuthorityCell v={c}/>
            </span>
          ))}
        </div>
      ))}
    </div>
  );
}

function AuthorityCell({ v }) {
  const t = window.tToken;
  if (v === "C") return <span style={{
    display: "inline-block", width: 18, lineHeight: "16px",
    fontFamily: t.fontMono, fontSize: 10, fontWeight: 700, color: t.paper, background: t.ink,
  }}>C</span>;
  if (v === "F") return <span style={{
    display: "inline-block", width: 18, lineHeight: "16px",
    fontFamily: t.fontMono, fontSize: 10, fontWeight: 700, color: t.accentY,
    border: `1px solid ${t.accentY}`,
  }}>F</span>;
  if (v === "A") return <span style={{
    display: "inline-block", width: 18, lineHeight: "16px",
    fontFamily: t.fontMono, fontSize: 10, color: t.ink3, border: `1px dotted ${t.ink4}`,
  }}>a</span>;
  if (v === "X") return <span style={{ color: t.accentR, fontSize: 12 }}>✕</span>;
  return <span style={{ color: t.ink4 }}>—</span>;
}

function TwoAxis() {
  const t = window.tToken;
  // x = cost (left cheap, right expensive); y = authority (top auth, bottom advisory)
  const w = 380, h = 200, pad = 28;
  const pts = [
    { name: "dxgi.queryAdapter",     cost: 0.05, auth: 0.85 },
    { name: "vk.memory.budget",      cost: 0.55, auth: 0.95 },
    { name: "setupapi.enumDevices",  cost: 0.10, auth: 0.65 },
    { name: "vk.physicalDeviceProps",cost: 0.20, auth: 0.55 },
    { name: "igcl.metrics",          cost: 0.85, auth: 0.30, degraded: true },
    { name: "self.process",          cost: 0.05, auth: 1.00 },
  ];
  return (
    <div style={{ marginTop: 6 }}>
      <svg width={w} height={h}>
        {/* axes */}
        <line x1={pad} y1={h - pad} x2={w - pad/2} y2={h - pad} stroke={t.ink3}/>
        <line x1={pad} y1={pad/2} x2={pad} y2={h - pad} stroke={t.ink3}/>
        {/* quadrant divisions */}
        <line x1={(w - pad - pad/2) / 2 + pad} y1={pad/2} x2={(w - pad - pad/2) / 2 + pad} y2={h - pad}
              stroke={t.ink4} strokeDasharray="2 3"/>
        <line x1={pad} y1={(h - pad - pad/2) / 2 + pad/2} x2={w - pad/2} y2={(h - pad - pad/2) / 2 + pad/2}
              stroke={t.ink4} strokeDasharray="2 3"/>
        {/* axis labels */}
        <text x={pad} y={h - 6} fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>cheap →</text>
        <text x={w - pad/2 - 60} y={h - 6} fontFamily={t.fontMono} fontSize="9" fill={t.ink3} textAnchor="start">→ expensive</text>
        <text x={4} y={pad/2 + 10} fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>authoritative</text>
        <text x={4} y={h - pad - 4} fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>advisory</text>
        {/* points */}
        {pts.map((p, i) => {
          const x = pad + p.cost * (w - pad - pad/2);
          const y = (h - pad) - p.auth * (h - pad - pad/2);
          const color = p.degraded ? t.accentY : t.ink;
          return (
            <g key={i}>
              <circle cx={x} cy={y} r={4} fill={p.degraded ? t.paper : color} stroke={color}/>
              <text x={x + 7} y={y + 3} fontFamily={t.fontMono} fontSize="9" fill={t.ink2}>{p.name}</text>
            </g>
          );
        })}
      </svg>
    </div>
  );
}

function th3(t) {
  return { textAlign: "left", padding: "4px 6px", borderBottom: `1px solid ${t.ink3}`, fontWeight: 600 };
}
function td3(t, color) {
  return { padding: "3px 6px", color: color || t.ink };
}

window.CostArtboard = CostArtboard;
