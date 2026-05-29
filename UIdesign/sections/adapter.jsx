// adapter.jsx — S2: adapter detail. Reconciled identity, provenance, state.
// refinement pass: identity grouped by origin (no per-row borders),
// confidence only shown when degraded, picked-source bracket, baseline deltas.
const AdapterArtboard = () => {
  const t = window.tToken;
  return (
    <Frame kicker="S2 · adapter detail" title="A1 · Intel Arc Pro B70"
      subtitle="pci 81:00.0 · luid 0x0000…00b1 · vk-uuid 6b2c8a-…-f0e1"
      footer="identity fields grouped by origin. confidence marks appear only where degraded. unsupported fields are hatched — knowing what is not measurable on this build is part of the answer.">
      <div style={{ border: `1px solid ${t.ink}`, background: t.paper,
                    display: "grid", gridTemplateColumns: "1.05fr 1fr", gridTemplateRows: "auto auto 1fr",
                    height: "100%" }}>

        {/* IDENTITY STRIP */}
        <div style={{ gridColumn: "1 / -1", padding: "10px 16px",
                      borderBottom: `1px solid ${t.ink3}`,
                      display: "flex", alignItems: "center", gap: 16 }}>
          <span style={{ fontSize: 24, fontWeight: 700, lineHeight: 1 }}>A1</span>
          <div>
            <div style={{ fontWeight: 600 }}>Intel Arc Pro B70 · 32 GiB</div>
            <div style={{ color: t.ink3, fontSize: 11 }}>active session vk-split-32b-q4 · participating since 14:08:02</div>
          </div>
          <div style={{ marginLeft: "auto", display: "flex", gap: 6 }}>
            <Chip variant="ok">D0 · active</Chip>
            <Chip variant="warn">1 disagreement</Chip>
            <Chip variant="muted">ReBAR · 32 GiB</Chip>
            <Chip variant="info">canonical · vk</Chip>
          </div>
        </div>

        {/* TABS */}
        <div style={{ gridColumn: "1 / -1", borderBottom: `1px solid ${t.ink4}` }}>
          <Tabs items={["identity", "memory", "activity", "collectors", "state log", "fingerprint"]} active={0}
                style={{ padding: "0 16px" }}/>
        </div>

        {/* LEFT: reconciled identity, grouped by origin */}
        <div style={{ borderRight: `1px solid ${t.ink4}`, padding: "12px 16px" }}>
          <H><span>reconciled identity</span>
            <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
              4 sources merged · grouped by origin · confidence shown only where &lt; 90%
            </span>
          </H>

          <IdGroup title="topology" sources={["setupapi", "vk", "dxgi"]} confNote="all sources agree">
            <IdRow k="PCI bus:dev.fn" v="81:00.0"/>
            <IdRow k="PCI vendor:dev" v="0x8086 : 0xE20B"/>
            <IdRow k="device instance" v="PCI\VEN_8086&DEV_E20B&…"/>
          </IdGroup>

          <IdGroup title="windows" sources={["dxgi", "setupapi"]}>
            <IdRow k="DXGI LUID" v="0x0000000000…00b1"/>
            <IdRow k="DXGI description" v="Intel(R) Arc(TM) Pro B70 Graphics"/>
            <IdRow k="driver INF" v="iigd_dch.inf"/>
            <IdRow k="WDDM version" v="3.1"/>
          </IdGroup>

          <IdGroup title="vulkan" sources={["vk"]}>
            <IdRow k="vk physical idx" v="1"/>
            <IdRow k="vk UUID" v="6b2c8a-…-f0e1"/>
            <IdRow k="vk device name" v="Intel Arc Pro B70 Graphics"/>
            <IdRow k="api version" v="1.3.290"/>
            <IdRow k="VRAM (advertised)" v="32.0 GiB"/>
            <IdRow k="ReBAR" v="enabled · 32 GiB BAR"/>
            <IdRow k="queue families" v="graphics+compute · compute · transfer"/>
          </IdGroup>

          <IdGroup title="toasty" sources={["toasty"]}>
            <IdRow k="VkDevice opened?" v="no — TrulyPassive"/>
            <IdRow k="GPU allocations" v="0"/>
            <IdRow k="first seen" v="14:02:11" muted/>
            <IdRow k="identity stability" v="stable since open" muted/>
          </IdGroup>

          <div style={{ fontSize: 10, color: t.ink3, marginTop: 10, lineHeight: 1.55 }}>
            reconciliation rule: PCI bus:dev.fn matches first, then LUID↔UUID via vendor:dev. conflicts emit an <Src name="identity.split"/> event.
          </div>
        </div>

        {/* RIGHT: memory, activity, state log */}
        <div style={{ padding: "12px 16px", display: "flex", flexDirection: "column", gap: 16 }}>

          {/* Memory */}
          <div>
            <H><span>memory</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                multi-source · 1 active disagreement · canonical pick: vk
              </span>
            </H>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr", gap: 16, marginTop: 8 }}>
              <div>
                <div style={{ fontSize: 10, color: t.ink3, textTransform: "uppercase", letterSpacing: 0.8 }}>
                  resident · canonical <Src name="vk"/>
                </div>
                <div style={{ display: "flex", alignItems: "baseline", gap: 8, marginTop: 4 }}>
                  <Val kind="disputed" style={{ fontSize: 22 }}>18.9</Val>
                  <span style={{ color: t.ink3, fontSize: 12 }}>/ 32.0 GiB</span>
                </div>
                <div style={{ fontSize: 10, color: t.ink3 }}>
                  baseline 5.3 GiB · <span style={{ color: t.accentY }}>Δ +13.6 GiB</span> · σ_5m = 0.6
                </div>
                <div style={{ marginTop: 4 }}>
                  <BaselineGhostSparkB trace={[5,6,7,9,14,18,18,19,19,19,19,19,18.9]} baseline={5.3} w={260} h={36}/>
                </div>
              </div>
              <div>
                <div style={{ fontSize: 10, color: t.ink3, textTransform: "uppercase", letterSpacing: 0.8 }}>
                  budget / heap · <Src name="vk"/> VK_EXT_memory_budget
                </div>
                <div style={{ fontSize: 14, marginTop: 4 }}>
                  budget <Val>30.4</Val> · usage <Val>18.9</Val> <span style={{ color: t.ink3 }}>GiB</span>
                </div>
                <BudgetBar used={18.9} budget={30.4} cap={32}/>
                <div style={{ fontSize: 10, color: t.ink3, marginTop: 4 }}>
                  budget pressure <b>62%</b> · headroom 11.5 GiB · trend → flat 60s
                </div>
              </div>
            </div>

            {/* Active disagreement panel — emphasize picked, dim rejected */}
            <div style={{ marginTop: 10, borderLeft: `2px solid ${t.accentY}`, padding: "4px 0 4px 10px" }}>
              <div style={{ display: "flex", alignItems: "baseline", gap: 8, fontSize: 11 }}>
                <Chip variant="warn">disagree</Chip>
                <b>vram.resident</b>
                <span style={{ color: t.ink3 }}>since 14:11:03 · 3m 05s · stable choice</span>
              </div>
              <div style={{ display: "grid", gridTemplateColumns: "12px 60px 90px 1fr auto",
                            gap: 6, marginTop: 6, fontSize: 11, alignItems: "center" }}>
                {[
                  { src: "vk",      v: "18.9 GiB", picked: true,  why: "rule: vk-budget-canonical (closest to allocator truth)" },
                  { src: "dxgi",    v: "20.1 GiB", picked: false, why: "QueryVideoMemoryInfo · CurrentUsage · Δ +1.2" },
                  { src: "taskmgr", v: "22.3 GiB", picked: false, why: "WDDM incl. shared + reserved · Δ +3.4 · informational" },
                ].map((s, i) => (
                  <React.Fragment key={i}>
                    <div style={{ color: s.picked ? t.ink : t.ink4, fontWeight: 700, textAlign: "center" }}>
                      {s.picked ? "▸" : ""}
                    </div>
                    <div><Src name={s.src} dim={!s.picked}/></div>
                    <div style={{ fontWeight: s.picked ? 700 : 400, color: s.picked ? t.ink : t.ink3 }}>
                      {s.v}
                    </div>
                    <div style={{ color: t.ink3, fontStyle: "italic" }}>{s.why}</div>
                    <div>{s.picked && <Chip variant="info" style={{ fontSize: 9 }}>canonical</Chip>}</div>
                  </React.Fragment>
                ))}
              </div>
            </div>
          </div>

          {/* Activity */}
          <div>
            <H><span>activity</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                indirect — toasty does not query engine utilization
              </span>
            </H>
            <div style={{ display: "grid", gridTemplateColumns: "1fr 1fr 1fr", gap: 14, marginTop: 6,
                          fontSize: 11.5 }}>
              <StatLine label="adapter wake"  v={<Val>D0 / awake</Val>}    sub="since 14:08:02"   src="dxgi"/>
              <StatLine label="engine clock"  v={<Val kind="estimated">2300</Val>}  sub="MHz · vs base 800 · sampled p50"  src="igcl" extra={<Slope dir={1}/>} sampled={12}/>
              <StatLine label="memory clock"  v={<Val kind="unsupported"/>}  sub="igcl stalled · vk has no field" src="igcl" degraded/>
            </div>
          </div>

          {/* State log */}
          <div>
            <H><span>state log</span>
              <span style={{ marginLeft: 8, fontWeight: 400, textTransform: "none", letterSpacing: 0, color: t.ink3 }}>
                D-state &amp; AdapterState transitions only
              </span>
            </H>
            <div style={{ fontSize: 11, marginTop: 6 }}>
              {[
                ["14:13:48", "collector.igcl",  "ok → stalled (>200ms)",       "warn"],
                ["14:08:02", "AdapterState",    "Idle → ActiveObserved",        "info"],
                ["14:08:02", "dxgi.power",      "D3 → D0",                      "ok"],
                ["14:04:11", "AdapterState",    "Baseline → Idle",              "muted"],
                ["14:02:11", "AdapterState",    "Unknown → Baseline",           "muted"],
              ].map((r, i) => (
                <div key={i} style={{
                  display: "grid", gridTemplateColumns: "70px 130px 1fr 60px",
                  padding: "3px 0", borderTop: i === 0 ? "none" : `1px dotted ${t.ink4}`,
                }}>
                  <span style={{ color: t.ink3 }}>{r[0]}</span>
                  <span>{r[1]}</span>
                  <span>{r[2]}</span>
                  <span style={{ textAlign: "right" }}><Chip variant={r[3]}>{r[3]}</Chip></span>
                </div>
              ))}
            </div>
          </div>
        </div>
      </div>
    </Frame>
  );
};

// ---------- helpers ----------
function IdGroup({ title, sources, confNote, children }) {
  const t = window.tToken;
  return (
    <div style={{ marginTop: 10 }}>
      <div style={{
        display: "flex", alignItems: "baseline", gap: 8,
        fontSize: 9, textTransform: "uppercase", letterSpacing: 1.5,
        color: t.ink3, paddingBottom: 3,
        borderBottom: `1px solid ${t.ink4}`,
      }}>
        <span style={{ fontWeight: 700 }}>{title}</span>
        <span style={{ display: "flex", gap: 3 }}>
          {sources.map((s, i) => <Src key={i} name={s}/>)}
        </span>
        {confNote && <span style={{ marginLeft: "auto", fontWeight: 400, letterSpacing: 0, textTransform: "none", color: t.ink4 }}>{confNote}</span>}
      </div>
      <div style={{ padding: "4px 0" }}>{children}</div>
    </div>
  );
}

function IdRow({ k, v, conf, muted }) {
  const t = window.tToken;
  return (
    <div style={{
      display: "grid", gridTemplateColumns: "160px 1fr auto",
      gap: 10, padding: "2px 0", fontSize: 11.5, alignItems: "baseline",
    }}>
      <span style={{ color: t.ink3 }}>{k}</span>
      <span style={{
        fontFamily: t.fontMono, color: muted ? t.ink3 : t.ink,
        overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap",
      }}>{v}</span>
      <span>{conf != null && conf < 0.9 && <Conf value={conf}/>}</span>
    </div>
  );
}

function StatLine({ label, v, sub, src, extra, degraded, sampled }) {
  const t = window.tToken;
  return (
    <div style={{
      padding: "6px 0", borderTop: `1px solid ${t.ink4}`,
      borderBottom: `1px solid ${t.ink4}`,
    }}>
      <div style={{ fontSize: 9, color: t.ink3, textTransform: "uppercase", letterSpacing: 0.8 }}>{label}</div>
      <div style={{ display: "flex", alignItems: "baseline", gap: 6, marginTop: 2 }}>
        <span style={{ fontSize: 14, fontWeight: 600 }}>{v}</span>
        {extra}
      </div>
      <div style={{ fontSize: 10, color: degraded ? t.accentY : t.ink3, marginTop: 2 }}>
        <Src name={src} dim={degraded}/> <span style={{ marginLeft: 4 }}>{sub}</span>
        {sampled && <span style={{ marginLeft: 6 }}>· n={sampled}</span>}
      </div>
    </div>
  );
}

function BaselineGhostSparkB({ trace, baseline, w = 260, h = 36 }) {
  const t = window.tToken;
  const all = [...trace, baseline];
  const max = Math.max(...all), min = Math.min(...all, 0);
  const r = max - min || 1;
  const y = (val) => h - 4 - ((val - min) / r) * (h - 8);
  const pts = trace.map((d, i) => `${(i / (trace.length - 1)) * w},${y(d)}`).join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block" }}>
      <line x1="0" y1={y(baseline)} x2={w} y2={y(baseline)} stroke={t.ink4} strokeDasharray="3 3"/>
      <text x={w - 2} y={y(baseline) - 2} fontFamily={t.fontMono} fontSize="8" fill={t.ink4} textAnchor="end">
        baseline {baseline}
      </text>
      <polyline points={pts} fill="none" stroke={t.ink} strokeWidth="1.4"/>
    </svg>
  );
}

function BudgetBar({ used, budget, cap }) {
  const t = window.tToken;
  const w = 260;
  return (
    <div style={{ marginTop: 4 }}>
      <svg width={w} height="14" viewBox={`0 0 ${w} 14`}>
        <rect x="0" y="3" width={w} height="8" fill="none" stroke={t.ink3}/>
        <rect x="0" y="3" width={(used / cap) * w} height="8" fill={t.ink}/>
        <line x1={(budget / cap) * w} x2={(budget / cap) * w} y1="0" y2="14" stroke={t.accentY} strokeWidth="1.2"/>
      </svg>
      <div style={{ fontSize: 9, color: t.accentY, marginTop: 0, marginLeft: (budget / cap) * w - 18 }}>budget</div>
    </div>
  );
}

window.AdapterArtboard = AdapterArtboard;
