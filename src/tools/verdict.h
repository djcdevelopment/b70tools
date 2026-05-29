#pragma once

#include <filesystem>

namespace b70 {

// Gate verdict subcommand. Reads events.jsonl, applies a small set of safety
// + load-distribution checks, prints a structured verdict, and returns:
//   0 — healthy (gates pass; safe to proceed)
//   2 — broken  (a hard gate failed; refuse to proceed)
//   3 — insufficient-data (not enough samples in the window to decide)
//
// Designed to be called from the eval runner just after a model load completes
// (b70tools running in parallel, llama-server up, optional warmup prompt fired)
// to decide whether the workload landed in a configuration that is safe for the
// rig and useful for benchmarking.
struct VerdictOptions {
    // host-RAM safety floor — protect against the OOM cascade that has caused
    // a non-POST / BIOS-reflash on this rig before
    double max_host_used_gb        = 26.0;

    // IGCL "card-0 is actually doing work" thresholds — applied to the healthy
    // adapter (adapter_00011b4f). Broken-IGCL adapter (adapter_00012fbe) has
    // no ongoing telemetry; we cannot observe it directly.
    std::string healthy_adapter    = "adapter_00011b4f";
    double min_card0_avg_w         = 30.0;
    double min_card0_activity_pct  = 5.0;

    // window: how many seconds back from the end of the file to use for the
    // "current state" check. 0 means use the full file span.
    double window_s                = 60.0;

    // VRAM distribution gates (applied when PDH cross-process VRAM is available).
    // Set to <=0 to disable a specific check.
    //
    // The PRIMARY safety signal is `max_per_card_nonlocal_gb` — Shared GPU
    // Memory commit is the smoking gun for the OOM cascade that risks BIOS
    // reflash. Per-card minimum and asymmetry are secondary (informational
    // for split-quality, not safety).
    //
    // Rationale for permissive defaults on min/asymmetry: a model that fits
    // on one B70 (eg Qwen 2.5 32B Q4 at ~20 GB + unified-KV ~4 GB = 24 GB) will
    // legitimately load single-card under llama.cpp's current behavior, even
    // with `-sm layer -ts 1,1`. That's a valid configuration, not a failure.
    // The asymmetry gate only fires together with elevated non_local commit
    // (which is the actual BIOS-flash risk).
    double min_per_card_local_gb    = 0.5;   // catches the "card never touched" pattern
    double max_per_card_nonlocal_gb = 2.0;   // hard safety floor — Shared GPU Memory cascade
    double max_asymmetry_ratio      = 0.0;   // 0 = disabled; only meaningful for models that should span both cards

    bool   emit_json               = false;
};

int run_verdict_command(const std::filesystem::path& arg, const VerdictOptions& opts = {});

}
