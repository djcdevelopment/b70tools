// components.jsx — wireframe primitives for the toasty observability sketches.
// Paper-and-ink lo-fi: warm off-white ground, dark ink, restrained accents.
// Two type voices: IBM Plex Mono for data/UI, Caveat for hand annotations.

const ink     = "#1a1a17";
const ink2    = "#3b3a35";
const ink3    = "#6b6a62";
const ink4    = "#a8a59a";
const paper   = "#f3efe5";
const paper2  = "#ebe6d8";
const paper3  = "#e2dcca";
const accentY = "#d4a017"; // disagreement / attention (mustard, not red)
const accentR = "#a8362a"; // hard error / unsupported
const accentG = "#3f6b3a"; // ok / steady
const accentB = "#2d5a7a"; // info / source-of-truth
const hatch   = "rgba(26,26,23,0.10)";

const fontMono  = "'IBM Plex Mono', ui-monospace, SFMono-Regular, Menlo, monospace";
const fontHand  = "'Caveat', 'Bradley Hand', cursive";
const fontSans  = "'Inter', system-ui, sans-serif";

// ---------- Frame: artboard wrapper. Plain wireframe paper, no decoration. ----------
function Frame({ title, subtitle, kicker, children, bg = paper, pad = 16, footer }) {
  return (
    <div style={{
      width: "100%", height: "100%", background: bg, color: ink,
      fontFamily: fontMono, fontSize: 12, lineHeight: 1.4,
      display: "flex", flexDirection: "column",
      boxSizing: "border-box", padding: pad,
    }}>
      {(title || kicker) && (
        <div style={{ display: "flex", alignItems: "baseline", gap: 10, marginBottom: 8,
                      borderBottom: `1px solid ${ink3}`, paddingBottom: 6 }}>
          {kicker && (
            <div style={{ fontFamily: fontMono, fontSize: 10, color: ink3, letterSpacing: 1.2,
                          textTransform: "uppercase" }}>
              {kicker}
            </div>
          )}
          {title && (
            <div style={{ fontFamily: fontMono, fontSize: 13, fontWeight: 600, letterSpacing: 0 }}>
              {title}
            </div>
          )}
          {subtitle && (
            <div style={{ fontFamily: fontMono, fontSize: 11, color: ink3 }}>
              {subtitle}
            </div>
          )}
        </div>
      )}
      <div style={{ flex: 1, minHeight: 0, position: "relative" }}>{children}</div>
      {footer && (
        <div style={{ marginTop: 8, fontFamily: fontMono, fontSize: 10, color: ink3,
                      borderTop: `1px solid ${ink4}`, paddingTop: 6 }}>
          {footer}
        </div>
      )}
    </div>
  );
}

// ---------- Panel: bordered region (sketch border via SVG) ----------
function Panel({ title, hint, children, style, pad = 12, bg = "transparent", border = ink, dashed = false }) {
  return (
    <div style={{
      position: "relative", padding: pad, background: bg,
      border: `1.2px ${dashed ? "dashed" : "solid"} ${border}`,
      borderRadius: 2,
      ...style,
    }}>
      {title && (
        <div style={{
          position: "absolute", top: -9, left: 10, background: bg === "transparent" ? paper : bg,
          padding: "0 6px", fontSize: 10, fontWeight: 600, letterSpacing: 0.6,
          textTransform: "uppercase", color: ink2,
        }}>{title}</div>
      )}
      {hint && (
        <div style={{
          position: "absolute", top: -10, right: 10, background: bg === "transparent" ? paper : bg,
          padding: "0 6px", fontFamily: fontHand, fontSize: 14, color: ink3,
        }}>{hint}</div>
      )}
      {children}
    </div>
  );
}

// ---------- Chip: small status pill ----------
function Chip({ children, variant = "muted", style }) {
  const palette = {
    ok:     { bg: "transparent", fg: accentG, bd: accentG },
    warn:   { bg: "transparent", fg: accentY, bd: accentY },
    err:    { bg: "transparent", fg: accentR, bd: accentR },
    info:   { bg: "transparent", fg: accentB, bd: accentB },
    muted:  { bg: "transparent", fg: ink3,    bd: ink4 },
    solid:  { bg: ink,           fg: paper,   bd: ink  },
  }[variant];
  return (
    <span style={{
      display: "inline-flex", alignItems: "center", gap: 4,
      padding: "1px 6px", border: `1px solid ${palette.bd}`,
      color: palette.fg, background: palette.bg, borderRadius: 2,
      fontFamily: fontMono, fontSize: 10, letterSpacing: 0.4,
      textTransform: "uppercase", whiteSpace: "nowrap",
      ...style,
    }}>{children}</span>
  );
}

// ---------- Source tag: [vk] [dxgi] [igcl] [setupapi] [taskmgr] ----------
function Src({ name, dim = false }) {
  return (
    <span style={{
      fontFamily: fontMono, fontSize: 10, color: dim ? ink4 : ink2,
      background: paper3, padding: "0 4px", borderRadius: 2,
      letterSpacing: 0, whiteSpace: "nowrap",
    }}>[{name}]</span>
  );
}

// ---------- DataRow: label / value / source / confidence ----------
function DataRow({ label, value, src, conf, dim, note, mono = true }) {
  return (
    <div style={{
      display: "grid", gridTemplateColumns: "minmax(120px,170px) 1fr auto auto",
      gap: 10, padding: "3px 0", borderBottom: `1px dotted ${ink4}`,
      alignItems: "baseline", color: dim ? ink3 : ink,
    }}>
      <div style={{ color: ink3, fontSize: 11 }}>{label}</div>
      <div style={{ fontFamily: mono ? fontMono : fontSans, fontSize: 12, color: dim ? ink3 : ink, overflow: "hidden", textOverflow: "ellipsis", whiteSpace: "nowrap" }}>
        {value}
        {note && <span style={{ marginLeft: 8, color: ink3, fontSize: 10 }}>· {note}</span>}
      </div>
      <div style={{ display: "flex", gap: 4 }}>
        {Array.isArray(src) ? src.map((s,i) => <Src key={i} name={s} dim={dim} />) : src && <Src name={src} dim={dim} />}
      </div>
      <div style={{ minWidth: 38, textAlign: "right", color: ink3, fontSize: 10 }}>
        {conf}
      </div>
    </div>
  );
}

// ---------- Ann: hand-drawn annotation (text + optional arrow) ----------
function Ann({ children, style, color = ink2, size = 16 }) {
  return (
    <div style={{
      position: "absolute", fontFamily: fontHand, fontSize: size,
      color, lineHeight: 1.1, whiteSpace: "pre-line", ...style,
    }}>{children}</div>
  );
}

// ---------- Arrow: sketch arrow between two points (SVG overlay) ----------
function Arrow({ from, to, label, curve = 0, dashed = false, color = ink2 }) {
  const [x1, y1] = from;
  const [x2, y2] = to;
  const mx = (x1 + x2) / 2 + curve;
  const my = (y1 + y2) / 2 - Math.abs(curve) * 0.3;
  return (
    <svg style={{ position: "absolute", inset: 0, width: "100%", height: "100%", pointerEvents: "none" }}>
      <defs>
        <marker id={`arr-${color.replace("#","")}`} viewBox="0 0 10 10" refX="9" refY="5"
                markerWidth="7" markerHeight="7" orient="auto-start-reverse">
          <path d="M0,0 L10,5 L0,10 z" fill={color}/>
        </marker>
      </defs>
      <path d={`M${x1},${y1} Q${mx},${my} ${x2},${y2}`}
            stroke={color} strokeWidth="1.2" fill="none"
            strokeDasharray={dashed ? "4 3" : ""}
            markerEnd={`url(#arr-${color.replace("#","")})`} />
      {label && (
        <text x={mx} y={my - 4} fontFamily={fontHand} fontSize="14" fill={color} textAnchor="middle">
          {label}
        </text>
      )}
    </svg>
  );
}

// ---------- Sparkline ----------
function Spark({ data, w = 80, h = 20, color = ink2, fill }) {
  const max = Math.max(...data), min = Math.min(...data);
  const r = max - min || 1;
  const pts = data.map((d, i) => `${(i / (data.length - 1)) * w},${h - ((d - min) / r) * h}`).join(" ");
  return (
    <svg width={w} height={h} style={{ display: "block" }}>
      {fill && <polygon points={`0,${h} ${pts} ${w},${h}`} fill={fill} />}
      <polyline points={pts} fill="none" stroke={color} strokeWidth="1.2" />
    </svg>
  );
}

// ---------- Hatch / unsupported pattern ----------
function Hatch({ w = "100%", h = 20, label = "unsupported", color = ink4 }) {
  return (
    <div style={{
      width: w, height: h, position: "relative", overflow: "hidden",
      background: `repeating-linear-gradient(135deg, ${hatch} 0 4px, transparent 4px 8px)`,
      border: `1px dashed ${color}`,
    }}>
      <div style={{
        position: "absolute", inset: 0, display: "flex", alignItems: "center", justifyContent: "center",
        fontFamily: fontMono, fontSize: 10, color, letterSpacing: 1, textTransform: "uppercase",
      }}>{label}</div>
    </div>
  );
}

// ---------- Key hint (e.g. ⌃K) ----------
function Key({ children }) {
  return (
    <kbd style={{
      fontFamily: fontMono, fontSize: 10, padding: "1px 5px",
      border: `1px solid ${ink3}`, borderBottomWidth: 2, borderRadius: 3,
      background: paper, color: ink2, lineHeight: 1.2,
    }}>{children}</kbd>
  );
}

// ---------- Faux button ----------
function Btn({ children, variant = "ghost", style }) {
  const v = {
    ghost:   { bg: "transparent", fg: ink,    bd: ink3 },
    solid:   { bg: ink,           fg: paper,  bd: ink  },
    danger:  { bg: "transparent", fg: accentR,bd: accentR },
    warn:    { bg: "transparent", fg: accentY,bd: accentY },
  }[variant];
  return (
    <span style={{
      display: "inline-flex", alignItems: "center", gap: 6,
      padding: "3px 9px", border: `1px solid ${v.bd}`, background: v.bg,
      color: v.fg, fontFamily: fontMono, fontSize: 11, borderRadius: 2,
      ...style,
    }}>{children}</span>
  );
}

// ---------- Box placeholder ----------
function Box({ label, h = 80, w = "100%", style, dashed = true, hand }) {
  return (
    <div style={{
      width: w, height: h, position: "relative",
      border: `1.2px ${dashed ? "dashed" : "solid"} ${ink3}`,
      display: "flex", alignItems: "center", justifyContent: "center",
      color: ink3, fontSize: 11, ...style,
    }}>
      {hand
        ? <span style={{ fontFamily: fontHand, fontSize: 18 }}>{label}</span>
        : <span>{label}</span>}
    </div>
  );
}

// ---------- Tab bar ----------
function Tabs({ items, active = 0, style }) {
  return (
    <div style={{ display: "flex", gap: 0, borderBottom: `1px solid ${ink3}`, ...style }}>
      {items.map((t, i) => (
        <div key={i} style={{
          padding: "5px 12px", fontSize: 11, letterSpacing: 0.3,
          borderBottom: i === active ? `2px solid ${ink}` : "2px solid transparent",
          color: i === active ? ink : ink3,
          background: i === active ? paper2 : "transparent",
          marginBottom: -1, cursor: "default",
        }}>{t}</div>
      ))}
    </div>
  );
}

// ---------- Confidence: quiet by default; loud only when degraded ----------
// Stable / high confidence: a single faint dot (or nothing if hide=true).
// Medium: small mustard bar. Low: red bar + numeric.
function Conf({ value = 1, hide = false, label }) {
  if (value >= 0.9) {
    if (hide) return null;
    return <span style={{ color: ink4, fontSize: 10, letterSpacing: 0 }}>·</span>;
  }
  if (value >= 0.7) {
    return (
      <span style={{ display: "inline-flex", alignItems: "center", gap: 4 }}>
        <span style={{ display: "inline-block", width: 18, height: 4, background: ink4 }}>
          <span style={{ display: "inline-block", width: 18 * value, height: 4, background: ink3, verticalAlign: "top" }}/>
        </span>
        {label && <span style={{ fontSize: 9, color: ink3 }}>{label}</span>}
      </span>
    );
  }
  const color = value < 0.4 ? accentR : accentY;
  return (
    <span style={{ display: "inline-flex", alignItems: "center", gap: 4 }}>
      <span style={{ display: "inline-block", width: 28, height: 5, background: paper3, border: `1px solid ${color}` }}>
        <span style={{ display: "inline-block", width: Math.max(2, 26 * value), height: 3, background: color, verticalAlign: "top", margin: 1 }}/>
      </span>
      <span style={{ fontSize: 9, color, fontFamily: fontMono }}>{label ?? `c=${value.toFixed(2)}`}</span>
    </span>
  );
}

// Legacy alias for any leftover call sites — same surface, but uses the new
// quiet-when-stable behavior. Strongly prefer <Conf/>.
function ConfBar({ value, w, label }) { return <Conf value={value} label={label}/>; }

// ---------- Val: a telemetry value with a qualifier ----------
// kinds:
//   canonical  (default) — plain mono
//   estimated  — "~" prefix, italic
//   inferred   — italic, paren note
//   stale      — dim, optional `age` annotation
//   disputed   — mustard dotted underline
//   sampled    — trailing "(n=…)" superscript
//   unsupported— hatched block
function Val({ children, kind = "canonical", age, n, note, style, weight = 600 }) {
  const base = { fontFamily: fontMono, fontSize: 13, fontWeight: weight, color: ink };
  if (kind === "unsupported") {
    return (
      <span style={{
        ...base, color: ink3, fontStyle: "italic", fontWeight: 400,
        background: `repeating-linear-gradient(135deg, ${hatch} 0 4px, transparent 4px 8px)`,
        padding: "0 6px", border: `1px dashed ${ink4}`,
      }}>—</span>
    );
  }
  const decor = {
    canonical:  {},
    estimated:  { fontStyle: "italic", color: ink2 },
    inferred:   { fontStyle: "italic", color: ink2 },
    stale:      { color: ink3 },
    disputed:   { textDecoration: `underline dotted ${accentY}`, textUnderlineOffset: 3 },
    sampled:    {},
  }[kind] || {};
  return (
    <span style={{ ...base, ...decor, ...style }}>
      {kind === "estimated" ? "~" : ""}{children}
      {kind === "sampled" && n != null && (
        <span style={{ fontSize: 9, color: ink3, marginLeft: 3, verticalAlign: "super", fontWeight: 400 }}>n={n}</span>
      )}
      {kind === "stale" && age && (
        <span style={{ fontSize: 10, color: ink3, marginLeft: 6, fontWeight: 400 }}>· {age} old</span>
      )}
      {note && (
        <span style={{ fontSize: 10, color: ink3, marginLeft: 6, fontStyle: "italic", fontWeight: 400 }}>{note}</span>
      )}
    </span>
  );
}

// ---------- DeltaTag: tiny "Δ vs baseline" annotation ----------
function Delta({ value, suffix = "", baseline, kind = "neutral" }) {
  const sign = value > 0 ? "+" : value < 0 ? "−" : "";
  const color = kind === "warn" ? accentY : kind === "ok" ? accentG : ink3;
  return (
    <span style={{ fontFamily: fontMono, fontSize: 10, color, letterSpacing: 0 }}>
      {sign}{Math.abs(value)}{suffix}{baseline != null && <span style={{ color: ink4 }}> vs {baseline}</span>}
    </span>
  );
}

// ---------- Slope: tiny inline indicator of recent direction ----------
function Slope({ dir = 0 }) {
  // dir: -1 down, 0 flat, +1 up
  const t = { color: ink3, fontSize: 10, fontFamily: fontMono };
  if (dir > 0) return <span style={t}>↗</span>;
  if (dir < 0) return <span style={t}>↘</span>;
  return <span style={t}>→</span>;
}

// ---------- Tiny dot indicator ----------
function Dot({ color = ink, size = 6 }) {
  return <span style={{
    display: "inline-block", width: size, height: size, borderRadius: size,
    background: color, verticalAlign: "middle", marginRight: 4,
  }}/>;
}

// ---------- Section heading inside a frame ----------
function H({ children, style }) {
  return <div style={{
    fontSize: 11, fontWeight: 600, letterSpacing: 0.8, textTransform: "uppercase",
    color: ink2, marginBottom: 6, ...style,
  }}>{children}</div>;
}

// Export to window so other Babel scripts can use.
Object.assign(window, {
  tToken: { ink, ink2, ink3, ink4, paper, paper2, paper3, accentY, accentR, accentG, accentB, hatch, fontMono, fontHand, fontSans },
  Frame, Panel, Chip, Src, DataRow, Ann, Arrow, Spark, Hatch, Key, Btn, Box, Tabs,
  Conf, ConfBar, Val, Delta, Slope, Dot, H,
});
