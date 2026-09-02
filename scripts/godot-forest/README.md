# Godot forest renderer harness

This harness launches an explicit Godot project; it does not assume a sibling
checkout. It captures the Lumberjacks forest lab's deterministic receipt beside
b70tools telemetry and optional PresentMon output.

```powershell
$project = 'C:\work\lumberjacks-platform\Lumberjacks\clients\godot-cs\nature-2.0'
$godot = 'C:\work\godot-4.6.1\editor\Godot_v4.6.1-stable_mono_win64\Godot_v4.6.1-stable_mono_win64_console.exe'

.\scripts\godot-forest\Start-GodotForestRun.ps1 -ProjectPath $project -GodotPath $godot -Renderer d3d12 -Preset default
.\scripts\godot-forest\Start-GodotForestRun.ps1 -ProjectPath $project -GodotPath $godot -Renderer vulkan -Preset default
.\scripts\godot-forest\Compare-GodotForestRuns.ps1 -RunRoot .\runs
```

Run each renderer three times for the actual decision. Pass `-GpuIndex` only
after checking the freshly written `adapter-enumeration.txt`; numeric Vulkan
indices are observations, not stable adapter identity. `-ExpectedAdapterId`
first guards against an absent physical adapter, then verifies the matching
adapter had the largest render/compute counter delta during the run. Every run
retains that stable LUID identity, the adapter activity ranking, the full
inventory, and post-run b70tools analyses.

PresentMon remains optional for an initial visual run. When supplied, pass its
exact arguments via `-PresentMonArgs`; `{csv}` expands to the run's frame CSV and
`{process}` expands to `Godot`.
