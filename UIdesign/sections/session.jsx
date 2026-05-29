// session.jsx — S1: live session view. Primary operational surface.
// refinement pass: fewer borders, baseline ghosting, sharper canonical pick,
// quieter confidence on stable fields, qualifier-aware values.
const SessionArtboard = () => {
  const t = window.tToken;
  return (
    <Frame kicker="S1 · session view" title="vk-split-32b-q4"
      subtitle="primary live screen · the renderer reads the same JSONL as replay"
      footer="every value below is stamped with the source that produced it. confidence marks appear only where data is degraded — quiet by default.">
      <div style={{ display: "flex", flexDirection: "column", height: "100%",
                    border: `1px solid ${t.ink}`, background: t.paper }}>

        {/* TOP STRIP --------------------------------------------------- */}
        <div style={{ display: "flex", alignItems: "center", gap: 14, padding: "8px 16px",
                      borderBottom: `1px solid ${t.ink3}` }}>
          <div style={{ fontSize: 10, color: t.ink3, letterSpacing: 1.2, textTransform: "uppercase" }}>session</div>
          <div style={{ fontSize: 13, fontWeight: 700 }}>vk-split-32b-q4</div>
          <Chip variant="ok">REC</Chip>
          <span style={{ color: t.ink3, fontSize: 11 }}>started 14:02:11 · elapsed 00:14:22</span>
          <span style={{ color: t.ink3, fontSize: 11 }}>· jsonl: session-2026-05-28-1402.jsonl <span style={{ color: t.ink4 }}>(4.2 MiB · 18,402 events)</span></span>
          <div style={{ marginLeft: "auto", display: "flex", gap: 6 }}>
            <Btn>pause</Btn>
            <Btn variant="warn">stop</Btn>
            <Btn>open in replay</Btn>
          </div>
        </div>

        {/* GLOBAL HEALTH STRIP — no per-field framing */}
        <div style={{ display: "flex", alignItems: "center", gap: 26, padding: "6px 16px",
                      borderBottom: `1px solid ${t.ink4}`, fontSize: 11, color: t.ink2 }}>
          <span><b>2</b> adapters <span style={{ color: t.ink3 }}>both D0</span></span>
          <span><b>4</b> collectors <span style={{ color: t.accentY, marginLeft: 4 }}>1 stalled</span></span>
          <span style={{ color: t.accentY }}><b>2</b> active disagreements</span>
          <span style={{ color: t.ink3 }}>poll 500 ms · last tick +12 ms</span>
          <span style={{ color: t.ink3 }}>self 1.4% cpu · 142 MiB rss</span>
          <span style={{ marginLeft: "auto", color: t.ink3 }}>0.4.2-a93f · vk 1.3.290 · win 10.0.19045</span>
        </div>

        {/* TABS — no padding box, just a thin rule */}
        <Tabs items={["overview", "adapters", "arbitration", "timeline", "cost", "events"]} active={0}
              style={{ padding: "0 16px" }}/>

        {/* BODY -------------------------------------------------------- */}
        <div style={{ flex: 1, display: "grid",
                      gridTemplateColumns: "1fr 1fr",
                      gridTemplateRows: "auto auto 1fr",
                      gap: 0 }}>

          {/* Adapter rows — full width, no inner panels */}
          <div style={{ gridColumn: "1 / -1", padding: "10px 16px 4px",
                        borderBottom: `1px solid ${t.ink4}` }}>
            <H>adapters</H>
            <AdapterRow
              id="A0" name="Intel Arc Pro B70" pci="03:00.0" state="D0 · active"
              vram={21.4} vramCap={32.0} vramBase={6.5} vramKind="canonical"
              clockEng={2300} clockMem={2100} clockMemKind="canonical"
              vramAgree={true}
              sources={["vk","dxgi","setupapi","igcl"]}
              spark={[6,7,8,12,18,20,21,21,22,21,21,21,21]}
              baselineSpark={[6.5, 6.4, 6.6, 6.5]}
            />
            <AdapterRow
              id="A1" name="Intel Arc Pro B70" pci="81:00.0" state="D0 · active"
              vram={18.9} vramCap={32.0} vramBase={5.3} vramKind="disputed"
              clockEng={2300} clockMem={null}
              vramAgree={false}
              sources={["vk","dxgi","setupapi","igcl(stall)"]}
              spark={[5,6,7,9,14,18,18,19,19,19,19,19,18.9]}
              baselineSpark={[5.3, 5.4, 5.3, 5.3]}
              disagree
              conf={0.61}
            />
          </div>

          {/* Arbitration — left half */}
          <div style={{ padding: "10px 16px", borderRight: `1px solid ${t.ink4}`,
                        borderBottom: `1px solid ${t.ink4}` }}>
            <H><span>active arbitration</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                2 disagreements · fallback ladder shown · explainable
              </span>
            </H>
            <ArbTable/>
          </div>

          {/* Event log — right half */}
          <div style={{ padding: "10px 16px", borderBottom: `1px solid ${t.ink4}` }}>
            <H><span>events</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                last 5m · <Key>/</Key> filter · <Key>j</Key>/<Key>k</Key> step
              </span>
            </H>
            <EventLog/>
          </div>

          {/* Timeline — full width */}
          <div style={{ gridColumn: "1 / -1", padding: "10px 16px" }}>
            <H><span>timeline</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                baseline 14:02–14:04 (ghosted) · cursor 14:14:08 · disagreement bands shown
              </span>
            </H>
            <SessionTimeline/>
          </div>
        </div>
      </div>
    </Frame>
  );
};

// ---------- helpers ----------
function AdapterRow({ id, name, pci, state, vram, vramCap, vramBase, vramKind,
                      clockEng, clockMem, clockMemKind,
                      vramAgree, sources, spark, baselineSpark, disagree, conf = 1 }) {
  const t = window.tToken;
  return (
    <div style={{
      display: "grid",
      gridTemplateColumns: "40px minmax(220px, 1fr) 260px 200px 130px 220px",
      gap: 14, alignItems: "center",
      padding: "10px 0", borderTop: `1px dotted ${t.ink4}`,
      fontFamily: t.fontMono, fontSize: 12,
    }}>
      <div style={{ fontSize: 22, fontWeight: 700, color: t.ink, lineHeight: 1 }}>{id}</div>
      <div style={{ overflow: "hidden" }}>
        <div style={{ fontWeight: 600 }}>{name}</div>
        <div style={{ color: t.ink3, fontSize: 11 }}>
          pci {pci} · vk-uuid {id === "A0" ? "6b2…a09f" : "6b2…f0e1"}
        </div>
        <div style={{ marginTop: 3, display: "flex", gap: 4 }}>
          {sources.map((s, i) =>
            <Src key={i} name={s.replace("(stall)","")} dim={s.includes("stall")}/>
          )}
        </div>
      </div>
      {/* VRAM */}
      <div>
        <div style={{ display: "flex", alignItems: "baseline", gap: 6 }}>
          <Val kind={vramKind} style={{ fontSize: 16 }}>{vram.toFixed(1)}</Val>
          <span style={{ color: t.ink3, fontSize: 11 }}>/ {vramCap.toFixed(1)} GiB</span>
          <Delta value={+(vram - vramBase).toFixed(1)} suffix=" GiB"
                 kind={disagree ? "neutral" : "neutral"}/>
        </div>
        <div style={{ fontSize: 10, color: t.ink3 }}>
          {vramAgree
            ? <>4 sources agree ±0.3</>
            : <>vk <b>18.9</b> · dxgi 20.1 · taskmgr 22.3 <span style={{ color: t.accentY }}>(disputed since 14:11)</span></>}
        </div>
      </div>
      {/* Clocks */}
      <div>
        <div style={{ fontSize: 12 }}>
          eng <Val>{clockEng}</Val> <span style={{ color: t.ink3 }}>MHz</span>
        </div>
        <div style={{ fontSize: 12 }}>
          mem {clockMem == null
            ? <Val kind="unsupported"/>
            : <><Val>{clockMem}</Val> <span style={{ color: t.ink3 }}>MHz</span></>}
        </div>
        <div style={{ fontSize: 10, color: t.ink3 }}>
          {clockMem == null
            ? <>igcl stalled · falling back · vk has no field</>
            : <>via igcl · jitter ±20</>}
        </div>
      </div>
      {/* State */}
      <div>
        <div style={{ fontSize: 12 }}>{state}</div>
        <div style={{ fontSize: 10, color: t.ink3 }}>awake since 14:08:02</div>
      </div>
      {/* Sparkline with baseline ghost */}
      <div>
        <BaselineGhostSpark trace={spark} baseline={vramBase} w={210} h={36}/>
        <div style={{ fontSize: 10, color: t.ink3, display: "flex", justifyContent: "space-between" }}>
          <span>vram · 5m · grey = baseline</span>
          {conf < 0.9 && <Conf value={conf}/>}
        </div>
      </div>
    </div>
  );
}

function BaselineGhostSpark({ trace, baseline, w = 210, h = 36 }) {
  const t = window.tToken;
  const all = [...trace, baseline, baseline * 1.05];
  const max = Math.max(...all), min = Math.min(...all, 0);
  const r = max - min || 1;
  const y = (v) => h - 4 - ((v - min) / r) * (h - 8);
  const pts = trace.map((d, i) => `${(i / (trace.length - 1)) * w},${y(d)}`).join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block" }}>
      {/* baseline horizontal ghost band */}
      <line x1="0" y1={y(baseline)} x2={w} y2={y(baseline)} stroke={t.ink4} strokeDasharray="3 3"/>
      <text x={w - 2} y={y(baseline) - 2} fontFamily={t.fontMono} fontSize="8" fill={t.ink4} textAnchor="end">baseline {baseline}</text>
      <polyline points={pts} fill="none" stroke={t.ink} strokeWidth="1.4"/>
    </svg>
  );
}

function ArbTable() {
  const t = window.tToken;
  const rows = [
    {
      metric: "vram.resident · A1", started: "14:11:03", lasted: "3m 05s", stability: "stable",
      sources: [
        { src: "vk",       value: "18.9 GiB",  conf: 0.95, picked: true,  why: "rule: vk-budget-canonical" },
        { src: "dxgi",     value: "20.1 GiB",  conf: 0.70, picked: false, why: "Δ +1.2 · advisory" },
        { src: "taskmgr",  value: "22.3 GiB",  conf: 0.40, picked: false, why: "WDDM incl. shared · informational" },
      ],
    },
    {
      metric: "clock.memory · A1", started: "14:13:48", lasted: "00:34s", stability: "newly degraded",
      sources: [
        { src: "igcl", value: "stalled", conf: 0.05, picked: false, why: "210 ms · DriverPassive flagged" },
        { src: "vk",   value: "—",       conf: 0.00, picked: true,  why: "no field · canonical n/a · unsupported" },
      ],
    },
  ];
  return (
    <div style={{ display: "flex", flexDirection: "column", gap: 14, marginTop: 4 }}>
      {rows.map((r, i) => (
        <div key={i}>
          <div style={{ display: "flex", alignItems: "baseline", gap: 10, marginBottom: 4 }}>
            <div style={{ fontWeight: 600, fontSize: 12 }}>{r.metric}</div>
            <Chip variant="warn">disagree · {r.stability}</Chip>
            <span style={{ marginLeft: "auto", fontSize: 10, color: t.ink3 }}>since {r.started} · {r.lasted}</span>
          </div>
          <div style={{ display: "grid", gridTemplateColumns: "12px 60px 110px 1fr auto",
                        gap: 6, fontSize: 11, alignItems: "center" }}>
            {r.sources.map((s, j) => {
              const isPicked = s.picked;
              return (
                <React.Fragment key={j}>
                  <div style={{ color: isPicked ? t.ink : t.ink4, fontWeight: 700, textAlign: "center" }}>
                    {isPicked ? "▸" : ""}
                  </div>
                  <div><Src name={s.src} dim={!isPicked}/></div>
                  <div style={{
                    fontFamily: t.fontMono, fontWeight: isPicked ? 700 : 400,
                    color: isPicked ? t.ink : t.ink3,
                  }}>
                    {s.value === "—" ? <Val kind="unsupported"/> : s.value}
                  </div>
                  <div style={{ color: t.ink3, fontStyle: "italic" }}>{s.why}</div>
                  <div>{!isPicked && s.conf < 0.7 && <Conf value={s.conf}/>}</div>
                </React.Fragment>
              );
            })}
          </div>
        </div>
      ))}
      <div style={{ fontSize: 10, color: t.ink3, lineHeight: 1.5 }}>
        ▸ = canonical pick · italic = rationale. fallback ladder configurable in <b>settings → fallback rules</b>.
        a switch of picked source emits a discrete <Src name="arbitration.switch"/> event in the log.
      </div>
    </div>
  );
}

function EventLog() {
  const t = window.tToken;
  const events = [
    ["14:14:08", "tick",        "poll +12ms · all collectors reporting", "muted"],
    ["14:13:48", "collector",   "igcl.metrics → stalled (210ms)",        "warn"],
    ["14:13:48", "fallback",    "clock.memory.A1 → vk (unsupported)",    "warn"],
    ["14:11:03", "disagree",    "vram.resident.A1  vk≠dxgi≠taskmgr",     "warn"],
    ["14:08:02", "workload",    "begin · external pid 8132",             "info"],
    ["14:08:02", "state",       "A0,A1 → D0/active",                     "info"],
    ["14:04:11", "baseline",    "complete · σ_vram=0.3 GiB",              "ok"],
    ["14:02:11", "session",     "open · capabilities sealed",            "ok"],
  ];
  return (
    <div style={{ marginTop: 4 }}>
      <div style={{ display: "grid", gridTemplateColumns: "70px 80px 1fr 50px",
                    fontSize: 9, color: t.ink3, textTransform: "uppercase",
                    letterSpacing: 1, paddingBottom: 3,
                    borderBottom: `1px solid ${t.ink4}` }}>
        <span>time</span><span>kind</span><span>detail</span><span style={{ textAlign: "right" }}>tag</span>
      </div>
      {events.map((e, i) => (
        <div key={i} style={{
          display: "grid", gridTemplateColumns: "70px 80px 1fr 50px",
          padding: "3px 0", fontSize: 11,
        }}>
          <span style={{ color: t.ink3 }}>{e[0]}</span>
          <span style={{ fontWeight: 600 }}>{e[1]}</span>
          <span>{e[2]}</span>
          <span style={{ textAlign: "right" }}>
            <Chip variant={e[3]} style={{ fontSize: 9 }}>{e[3]}</Chip>
          </span>
        </div>
      ))}
    </div>
  );
}

function SessionTimeline() {
  const t = window.tToken;
  return (
    <div>
      <svg width="100%" height="240" viewBox="0 0 1600 240" preserveAspectRatio="none">
        {/* Y-axis lane labels (no boxes) */}
        {[
          ["vram.A0",   30],
          ["vram.A1",   80],
          ["clk.A0",   130],
          ["clk.A1",   175],
          ["events",   215],
        ].map(([l, y]) => (
          <text key={l} x="4" y={y + 4} fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>{l}</text>
        ))}

        {/* baseline window — single faint band, no border */}
        <rect x="80" y="0" width="240" height="200" fill={t.paper2}/>
        <text x="90" y="12" fontFamily={t.fontMono} fontSize="9" fill={t.ink3} letterSpacing="1">BASELINE</text>

        {/* x-axis ticks (no grid lines, just labels) */}
        {[80, 320, 560, 800, 1040, 1280, 1520].map((x, i) => (
          <text key={x} x={x + 2} y="232" fontFamily={t.fontMono} fontSize="9" fill={t.ink4}>
            {["14:02","14:04","14:06","14:08","14:10","14:12","14:14"][i]}
          </text>
        ))}

        {/* baseline ghost lines (per lane) */}
        {[[46, 6.5], [96, 5.3], [142, 800], [187, 800]].map(([y], i) => (
          <line key={i} x1="0" x2="1600" y1={y} y2={y} stroke={t.ink4} strokeDasharray="2 4" strokeWidth="0.6"/>
        ))}

        {/* vram.A0 trace */}
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="80,46 320,44 560,42 800,18 1040,20 1280,22 1520,22"/>
        {/* vram.A1 trace */}
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="80,96 320,94 560,92 800,78 1040,72 1280,72 1520,72"/>
        {/* disagreement band on A1 (mustard underline-style band) */}
        <rect x="900" y="68" width="620" height="12" fill="none" stroke={t.accentY} strokeDasharray="3 3"/>
        <text x="900" y="66" fontFamily={t.fontMono} fontSize="9" fill={t.accentY}>vk≠dxgi · ongoing</text>

        {/* clk traces */}
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="80,142 320,142 560,142 800,128 1040,124 1280,124 1520,124"/>
        <polyline fill="none" stroke={t.ink} strokeWidth="1.3"
          points="80,187 320,187 560,187 800,170 1040,165 1280,165 1380,165"/>

        {/* unsupported region for clk.A1 — hatch */}
        <pattern id="hatchTL" patternUnits="userSpaceOnUse" width="6" height="6">
          <path d="M0,6 L6,0" stroke={t.ink4} strokeWidth="0.6"/>
        </pattern>
        <rect x="1380" y="160" width="140" height="12" fill="url(#hatchTL)"/>
        <text x="1390" y="171" fontFamily={t.fontMono} fontSize="9" fill={t.ink3}>fallback · unsupported</text>

        {/* events row markers */}
        {[
          { x: 80,  c: t.ink,    t: "session open" },
          { x: 320, c: t.accentG,t: "baseline end" },
          { x: 800, c: t.accentY,t: "workload begin" },
          { x: 900, c: t.accentY,t: "disagree A1" },
          { x: 1280,c: t.accentR,t: "igcl stall" },
        ].map((e, i) => (
          <g key={i}>
            <line x1={e.x} x2={e.x} y1="0" y2="208" stroke={e.c} strokeWidth="0.7" strokeDasharray="2 3"/>
            <rect x={e.x - 3} y={210} width="6" height="6" fill={e.c}/>
            <text x={e.x + 8} y={216} fontFamily={t.fontMono} fontSize="9" fill={t.ink2}>{e.t}</text>
          </g>
        ))}

        {/* cursor */}
        <line x1="1380" x2="1380" y1="0" y2="220" stroke={t.ink} strokeWidth="1.4"/>
        <g transform="translate(1384, 4)">
          <rect width="180" height="60" fill={t.paper} stroke={t.ink}/>
          <text x="8" y="14" fontFamily={t.fontMono} fontSize="10" fill={t.ink}>T = 14:14:08</text>
          <text x="8" y="28" fontFamily={t.fontMono} fontSize="10" fill={t.ink2}>vram.A1  18.9 GiB</text>
          <text x="8" y="40" fontFamily={t.fontMono} fontSize="9" fill={t.accentY}>Δ +13.6 vs baseline</text>
          <text x="8" y="54" fontFamily={t.fontMono} fontSize="9" fill={t.accentY}>disputed · vk picked</text>
        </g>
      </svg>
      <div style={{ display: "flex", gap: 16, marginTop: 6, fontSize: 10, color: t.ink3 }}>
        <span><Key>←</Key>/<Key>→</Key> step events</span>
        <span><Key>⇧</Key>+drag = zoom</span>
        <span><Key>b</Key> mark baseline</span>
        <span><Key>o</Key> overlay session</span>
        <span><Key>[</Key>/<Key>]</Key> jump to disagreement</span>
        <span style={{ marginLeft: "auto" }}>raw / smoothed: <b>raw</b></span>
      </div>
    </div>
  );
}

window.SessionArtboard = SessionArtboard;
