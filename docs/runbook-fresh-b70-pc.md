# Getting Started: Arc Pro B70 on a Fresh Windows PC

**Audience:** Someone who has just received one or more Intel Arc Pro B70 graphics cards
and wants to run Vulkan inference workloads, monitor GPU telemetry, and reproduce the
experiments documented in this repo.

**Reference rig:** 2× Arc Pro B70 · Ryzen 9 5900X · 32 GB DDR4 · Win 10 Pro 19045 ·
driver 32.0.101.8801

---

## Part 1 — Hardware Setup

### 1.1 Slot Selection

If you have two B70 cards:
- Slot 2 (lower, more airflow) is your primary Vulkan0 card. It will generally run
  5–12 °C cooler under load.
- Slot 1 (upper, cramped near CPU heatsink) is Vulkan1. Expect VRAM temps 10–15 °C
  higher than the lower card under identical load.
- Both cards deliver identical inference throughput — the slot difference is thermal
  only, not compute.

If you have one card: slot 2 (or the primary PCIe x16 slot).

### 1.2 ReBAR

Enable Above-4G Decoding and Resizable BAR in UEFI/BIOS. Both should be ON for best
performance. Verify after boot:
```powershell
Get-PnpDeviceProperty -InstanceId (Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Arc*" }).InstanceId DEVPKEY_Device_BusReportedDeviceDesc
```
Or check Device Manager → Display adapters → Intel Arc Pro B70 → Properties → Details →
"Bus Reported Device Description" — should show driver-reported VRAM size.

### 1.3 PCIe Bandwidth

The B70 is PCIe 5.0 capable. Most current platforms (Intel Z690/Z790, AMD X570/X670)
run it at PCIe 4.0 or even PCIe 3.0 depending on slot wiring. Dual-card on PCIe 4.0
runs at x8/x8. This is fine for inference — the bottleneck is not PCIe bandwidth.

### 1.4 ECC

ECC is configurable in Intel's driver control panel (or via IGCL API). ECC ON reduces
usable VRAM from 32 GB to ~28 GB per card. Choose based on workload:
- **ECC OFF** — 32 GB usable; most inference workloads; this rig's current config
- **ECC ON** — 28 GB usable; required if you need memory protection

---

## Part 2 — Windows Driver Installation

### 2.1 Install the Intel Arc Driver

Download from: https://www.intel.com/content/www/us/en/download-center/home.html
→ Graphics → Arc & Iris Xe Graphics

**Verified working driver for this repo's experiments:** 32.0.101.8801

Install the production driver package (includes IGCL, Arc Control app, OpenCL runtime).
Reboot after installation.

### 2.2 Verify Installation

```powershell
# Check device is recognized:
Get-PnpDevice | Where-Object { $_.FriendlyName -like "*Arc*" } | Select-Object FriendlyName, Status

# Check driver version:
Get-WmiObject Win32_VideoController | Where-Object { $_.Name -like "*Arc*" } |
    Select-Object Name, DriverVersion
```

Expected: `Status: OK`, `DriverVersion: 32.0.101.8801` (or your installed version).

### 2.3 Known Driver Issues (driver 32.0.101.8801 on Win10 19045)

These are rig-confirmed issues; they do not affect inference quality:

1. **IGCL telemetry on one card reads wrong registers.** `ctlPowerTelemetryGet` for
   the top-slot card reports 5.117 V and 8.55 GHz. These are bogus constant values.
   Temperature sensors on the same card are credible. The compute path is unaffected.

2. **D3DKMT `ADAPTERPERFDATA` query returns `STATUS_INVALID_PARAMETER`.** This is a
   known Windows 10 / this driver combination limitation. PDH and DXGI routes work
   correctly.

3. **IGCL may go silent under concurrent Vulkan init contention.** If two Vulkan
   applications initialize simultaneously, IGCL for the second card may stop emitting
   for the duration. Mitigation: start b70tools first (10-15 s lead time), then launch
   the workload.

---

## Part 3 — Vulkan Inference Setup (llama.cpp)

b70tools is a passive observer — it works with any Vulkan inference engine.
The reference setup for this repo is llama.cpp Vulkan build.

### 3.1 Download llama.cpp Vulkan

Pre-built Windows Vulkan binaries are available at:
https://github.com/ggml-org/llama.cpp/releases

Look for the `llama-*-bin-win-vulkan-x64.zip` asset. Extract to a local directory
(e.g., `D:\work\battlemage\llamacpp-win-vulkan\`). The key binaries:

```
llama-server.exe   — HTTP server, best for multi-turn eval
llama-cli.exe      — command-line generation, best for single-shot timing
```

### 3.2 Get a Model

Download a GGUF model from Hugging Face. Recommended starting points for dual-B70
(56–64 GB combined VRAM):

| Model | Quant | Size | Use |
|---|---|---|---|
| Qwen3-30B-A3B-Instruct-2507 | Q4_K_M | ~17 GB | Fast chat, 82 t/s gen on dual-B70 |
| Qwen2.5-32B-Instruct | Q4_K_M | ~18.5 GB | Good quality, 20 t/s gen |
| Qwen2.5-32B-Instruct | Q6_K | ~24 GB | High quality, slightly slower |
| Mistral-Small-3.2-24B-Instruct | Q4_K_M | ~14 GB | 27 t/s single-card OR dual |

Use `huggingface-cli` or a browser download to `D:\work\battlemage\models\`.

### 3.3 Environment Variables

Always set these before running llama.cpp on Arc:

```powershell
$env:GGML_VK_DISABLE_COOPMAT = '1'   # Required: Arc B70 coopmat has stability issues
```

For single-card (explicitly pick a device):
```powershell
$env:GGML_VK_VISIBLE_DEVICES = '0'   # Vulkan0 = bottom-slot card
$env:GGML_VK_VISIBLE_DEVICES = '1'   # Vulkan1 = top-slot card
```

For dual-card layer split (both visible, llama handles split):
```powershell
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
```

### 3.4 Launch the Server (dual-card layer split)

```powershell
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'

& "D:\work\battlemage\llamacpp-win-vulkan\llama-server.exe" `
    -m "D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" `
    -ngl 99 `
    -sm layer -ts 1,1 -fit off `
    --no-mmap -dio `
    -fa `
    -ctk q8_0 -ctv q8_0 `
    -c 32768 `
    --port 8080
```

Flag reference:
- `-ngl 99` — offload all layers to GPU
- `-sm layer -ts 1,1` — layer split 50/50 across both cards
- `-fit off` — **critical**: disables auto-fit which incorrectly routes all layers to one card
- `--no-mmap -dio` — disable mmap, use direct I/O for model load
- `-fa` — Flash Attention (improves performance)
- `-ctk q8_0 -ctv q8_0` — 8-bit KV cache (saves VRAM)

### 3.5 Quick Single-Shot Test

```powershell
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'

& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -m "D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" `
    -ngl 99 -sm layer -ts 1,1 -fit off `
    --no-mmap -dio -c 4096 -n 200 `
    -p "Describe the Intel Arc Pro B70 GPU in one paragraph."
```

Watch the output: it should print `llama_kv_cache_init ... GPU0 ... GPU1` showing weights
on both cards, then produce tokens. At the end: `eval time` and `sample time` show prompt
eval and generation throughput.

---

## Part 4 — Building b70tools

### 4.1 Prerequisites

- **Visual Studio 2022 Community** (free) — install the "Desktop development with C++"
  workload. This provides MSVC, CMake, and Ninja.
  https://visualstudio.microsoft.com/downloads/

- **Git** (optional, for cloning): https://git-scm.com/download/win

### 4.2 Clone or Copy the Repo

```powershell
git clone https://github.com/<owner>/b70tools.git D:\work\b70tools
```

Or copy the directory tree to `D:\work\b70tools`.

### 4.3 Build

```powershell
cd D:\work\b70tools
.\build.ps1
```

This locates MSVC 2022, configures with CMake + Ninja, and produces:
```
build\b70tools.exe   (~490 KB)
```

If the build script can't find MSVC, you can also use the Developer Command Prompt:
```cmd
cd D:\work\b70tools
configure-and-build.cmd
```

### 4.4 Verify the Build

```powershell
& "D:\work\b70tools\build\b70tools.exe" --enumerate --out "D:\work\b70tools\runs\preflight"
```

Expected output: two adapters reconciled, both Arc Pro B70, driver version printed.

---

## Part 5 — Running Telemetry

### 5.1 Pre-Flight Check

Before any experiment:

```powershell
& "D:\work\b70tools\build\b70tools.exe" --enumerate --out "D:\work\b70tools\runs\preflight"
& "D:\work\b70tools\build\b70tools.exe" summarize "D:\work\b70tools\runs\preflight"
```

Pass criteria:
- Both adapters reconciled (`Adapters (2)`)
- DXGI / SetupAPI / Vulkan all OK
- No `ambiguous` warning
- Driver version matches expected

### 5.2 Idle Baseline (required before inference experiments)

Capture 5 minutes at idle — no inference running:

```powershell
& "D:\work\b70tools\build\b70tools.exe" run --ticks 300 --out "D:\work\b70tools\runs\baseline-idle-1"
```

Analyze:
```powershell
& "D:\work\b70tools\build\b70tools.exe" adapters      "D:\work\b70tools\runs\baseline-idle-1"
& "D:\work\b70tools\build\b70tools.exe" disagreements "D:\work\b70tools\runs\baseline-idle-1"
& "D:\work\b70tools\build\b70tools.exe" self          "D:\work\b70tools\runs\baseline-idle-1"
```

Note the idle activity rates from `adapters` — you'll compare inference runs against these.
Expected idle: 3–6% `render_compute` activity on the healthy card.

### 5.3 Single-Card Inference Telemetry

**Important: start b70tools FIRST, then start the workload.** This gives IGCL 10-15 s to
settle before Vulkan contention begins.

```powershell
# Terminal 1 — start telemetry (3-minute window):
& "D:\work\b70tools\build\b70tools.exe" run --ticks 180 --out "D:\work\b70tools\runs\single-gpu-1"

# Terminal 2 — after 10-15 s, start inference:
$env:GGML_VK_VISIBLE_DEVICES = '0'
$env:GGML_VK_DISABLE_COOPMAT = '1'
& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -m "D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" `
    -ngl 99 --no-mmap -dio -c 4096 -n 1000 `
    -p "Write a detailed technical explanation of how GPUs execute matrix multiplication."
```

Analyze after:
```powershell
& "D:\work\b70tools\build\b70tools.exe" adapters "D:\work\b70tools\runs\single-gpu-1"
```

Look for the used card's `render_compute` rate to be ≥5× the idle baseline (~15–30%+).
Temperature delta of +20 °C or more is a strong confirmation signal.

### 5.4 Dual-Card Layer-Split Telemetry

**Operational hazard:** on this rig, a cold-start dual-card + near-VRAM-limit workload
can cascade into a near-total system lockup requiring reboot, memory retraining, and
sometimes BIOS reflash (hours of recovery). Mitigations:

1. Always start b70tools first (10-15 s lead time)
2. Do NOT push toward VRAM limits until v1.5's PDH VRAM collector is available
3. Use `verdict` to check safety before large runs: `& b70tools.exe verdict <run-dir>`

```powershell
# Terminal 1 — 10-minute telemetry window:
& "D:\work\b70tools\build\b70tools.exe" run --ticks 600 --out "D:\work\b70tools\runs\dual-b70-1"

# Terminal 2 — after 12-15 s, start dual-card inference:
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'
& "D:\work\battlemage\llamacpp-win-vulkan\llama-cli.exe" `
    -m "D:\work\battlemage\models\Qwen3-30B-A3B-Instruct-2507-Q4_K_M.gguf" `
    -ngl 99 -sm layer -ts 1,1 -fit off `
    --no-mmap -dio -c 4096 -n 2000 `
    -p "Explain the architecture of a modern LLM inference engine in detail."
```

Key analysis verb for dual-card runs:
```powershell
& "D:\work\b70tools\build\b70tools.exe" adapters "D:\work\b70tools\runs\dual-b70-1"
```

Both cards' die temperatures should rise above idle (~61 °C+ for MoE, ~66 °C+ for dense).
That's your primary "both cards are working" signal.

---

## Part 6 — Interpreting Results

### 6.1 Reading `b70tools adapters`

This is the primary inference diagnostic verb. It prints per-adapter:
- **Activity rate** (`render_compute` Δcounter / Δwall): idle ~3–6%, working ~15–35%
- **Temperature range** (peak is the signal; idle is the baseline)
- **Frequency envelope** (idle ~400-550 MHz, working ~1.5–2.8 GHz)
- **Voltage envelope** (idle ~0.72–0.775 V, working up to ~1.06 V)

Single-card working signal: used card activity rate ≥ 5× idle baseline.
Dual-card working signal: both cards' die temp ≥ +10 °C above idle.

### 6.2 Reading `b70tools disagreements`

Lists cross-source conflicts. On a fresh Arc Pro B70 with driver 32.0.101.8801,
expect exactly 3 classes at baseline:

```
expected_source_unavailable     — D3DKMT path unavailable (normal)
physically_impossible_voltage   — IGCL reads wrong register on one card (normal)
physically_impossible_frequency — IGCL reads wrong register on one card (normal)
```

Any 4th class is a real finding worth investigating.

### 6.3 Reading `b70tools self`

Shows observation cost:
- **RSS**: should be ~17 MB regardless of workload (all from Vulkan ICD)
- **Init wall**: should be ~50–130 ms; >300 ms means ICD contention (start b70tools first)
- **Do-no-harm budget**: should PASS (<50 MiB) in every run

### 6.4 Reading `b70tools verdict`

Aggregate validity verdict: exit 0 (clean), 2 (advisory), 3 (unsafe). Used by
automated scripts as a safety gate before runs that could stress VRAM budgets.

```powershell
& "D:\work\b70tools\build\b70tools.exe" verdict "D:\work\b70tools\runs\<name>"
$LASTEXITCODE   # 0 = clean, 2 = advisory, 3 = unsafe
```

---

## Part 7 — Common Issues & Fixes

| Symptom | Likely Cause | Fix |
|---|---|---|
| `adapters` shows one card at idle activity even during inference | `GGML_VK_VISIBLE_DEVICES` not set, or workload routed to wrong card | Set `$env:GGML_VK_VISIBLE_DEVICES = '0,1'` and use `-fit off` |
| Both cards showing identical 0% activity | Workload crashed or wrong binary | Check llama-cli output for errors; verify Vulkan build |
| System lockup / unresponsive UI during dual-card run | VRAM ceiling approached or IGCL cascade | Reboot; use smaller model or `-c 4096`; always start b70tools first |
| IGCL reports 5.117 V / 8.55 GHz on one card | Known driver bug on top-slot card | Expected; ignore voltage/freq on that card; temperature is credible |
| `b70tools self` shows >300 ms Vulkan init | Started b70tools after workload was already initializing | Start b70tools 10-15 s before the workload |
| JSONL file is empty or very small | b70tools exited before first tick | Use `--ticks 30` minimum; check for immediate crash with `--dry-run` |
| Can't find `b70tools.exe` | Build not run yet | Run `.\build.ps1` from repo root |
| Build fails: CMake not found | Visual Studio not installed or wrong workload | Install VS 2022 Community with "Desktop development with C++" |

---

## Part 8 — Thermal Management Tips

Based on empirical observations on this rig:

1. **Open case improves thermals.** The cramped top-slot card dropped from 90 °C VRAM
   (closed, no airflow) to 64–74 °C with improved forced airflow. Worth doing before
   sustained dual-card sessions.

2. **Temperature is your best "both cards working" signal.** When IGCL telemetry is
   degraded on one card (as on this rig), thermal delta is more reliable than activity
   counters.

3. **Watch VRAM temperature, not die temperature.** VRAM on GDDR6 is spec'd to ~105 °C;
   the B70's memory runs hotter than the die. The top-slot card routinely hits
   74–90 °C VRAM under solo Mistral 24B load with poor airflow. That's fine for short
   runs, concerning for sustained hours.

4. **Dual-split halves per-card thermal load.** Both cards at 61–67 °C under dual-split
   MoE/dense vs 73–74 °C for single-card solo Mistral 24B. Layer split is thermally
   better than running one card at full load.

5. **The `b70tools adapters` thermal envelope** after a run shows peak values — use those
   to assess whether your airflow setup is safe for longer sessions.

---

## Quick Reference

```powershell
# Build:
cd D:\work\b70tools; .\build.ps1

# Enumerate adapters:
& .\build\b70tools.exe --enumerate --out .\runs\preflight

# 5-min idle baseline:
& .\build\b70tools.exe run --ticks 300 --out .\runs\baseline-idle-1

# Single-card inference telemetry (3 min):
& .\build\b70tools.exe run --ticks 180 --out .\runs\single-gpu-test

# Dual-card layer-split telemetry (10 min):
& .\build\b70tools.exe run --ticks 600 --out .\runs\dual-b70-test

# Analysis verbs:
& .\build\b70tools.exe adapters      .\runs\<name>   # ← start here
& .\build\b70tools.exe summarize     .\runs\<name>
& .\build\b70tools.exe disagreements .\runs\<name>
& .\build\b70tools.exe self          .\runs\<name>
& .\build\b70tools.exe verdict       .\runs\<name>

# Environment for dual-card llama.cpp:
$env:GGML_VK_VISIBLE_DEVICES = '0,1'
$env:GGML_VK_DISABLE_COOPMAT = '1'
# llama-server flags: -ngl 99 -sm layer -ts 1,1 -fit off --no-mmap -dio -fa
```
