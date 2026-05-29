#pragma once

#include <windows.h>

// Minimal D3DKMT typedefs to avoid WDK header dependency.
// References: Microsoft's documented D3DKMT_ADAPTER_PERFDATA + d3dkmthk.h.
// Struct field layouts must match the running OS exactly.

extern "C" {

using D3DKMT_HANDLE = UINT32;

struct B70_D3DKMT_OPENADAPTERFROMLUID {
    LUID            AdapterLuid;
    D3DKMT_HANDLE   hAdapter;
};

struct B70_D3DKMT_CLOSEADAPTER {
    D3DKMT_HANDLE   hAdapter;
};

enum B70_KMTQUERYADAPTERINFOTYPE : INT {
    B70_KMTQAITYPE_ADAPTERPERFDATA = 79,
};

struct B70_D3DKMT_QUERYADAPTERINFO {
    D3DKMT_HANDLE                hAdapter;
    B70_KMTQUERYADAPTERINFOTYPE  Type;
    VOID*                        pPrivateDriverData;
    UINT                         PrivateDriverDataSize;
};

struct B70_D3DKMT_ADAPTER_PERFDATA {
    ULONG       PhysicalAdapterIndex;
    ULONGLONG   MemoryFrequency;        // Hz
    ULONGLONG   MaxMemoryFrequency;     // Hz
    ULONGLONG   MaxMemoryFrequencyOC;   // Hz
    ULONGLONG   MemoryBandwidth;        // B/s
    ULONGLONG   PCIEBandwidth;          // B/s
    ULONG       FanRPM;
    ULONG       Power;                  // tenths of a percent (per-mille of TDP)
    ULONG       Temperature;            // deci-Celsius
    UCHAR       PowerStateOverride;     // 0/1
};

using PFN_D3DKMTOpenAdapterFromLuid =
    LONG (APIENTRY *)(B70_D3DKMT_OPENADAPTERFROMLUID*);
using PFN_D3DKMTQueryAdapterInfo =
    LONG (APIENTRY *)(B70_D3DKMT_QUERYADAPTERINFO*);
using PFN_D3DKMTCloseAdapter =
    LONG (APIENTRY *)(const B70_D3DKMT_CLOSEADAPTER*);

// --- D3DKMTQueryStatistics (system-wide kernel adapter statistics) -----------
// Used to obtain cross-process VRAM commit per adapter per segment group.
// Reference: Microsoft d3dkmthk.h; layout is stable on Windows 10+ x64.

enum B70_D3DKMT_QUERYSTATISTICS_TYPE : INT {
    B70_D3DKMT_QS_VIDEO_MEMORY_SEGMENT_GROUP = 14,
};

#pragma pack(push, 8)

// VIDEO_MEMORY_SEGMENT_GROUP variant — system-wide per-segment-group VRAM stats.
// SegmentGroup is INPUT: 0 = local (dedicated VRAM), 1 = non_local (shared system memory).
// Kernel fills in CommitLimit / BytesCommitted / BytesResident / AggregatedAllocations.
struct B70_D3DKMT_QS_VIDEO_MEMORY_SEGMENT_GROUP_RESULT {
    ULONG        PhysicalAdapterIndex;
    UINT         SegmentGroup;
    ULONGLONG    CommitLimit;
    ULONGLONG    BytesCommitted;
    ULONGLONG    BytesResident;
    ULONGLONG    AggregatedAllocations;
    ULONGLONG    Padding[5];
};

// Outer struct passed to D3DKMTQueryStatistics. The QueryResult union has many
// variants; we declare only the one we use, with a generous tail buffer to
// cover any larger variant the kernel may write past our variant size.
struct B70_D3DKMT_QUERYSTATISTICS {
    B70_D3DKMT_QUERYSTATISTICS_TYPE Type;
    LUID                            AdapterLuid;
    HANDLE                          hProcess;
    union {
        B70_D3DKMT_QS_VIDEO_MEMORY_SEGMENT_GROUP_RESULT VideoMemorySegmentGroup;
        UCHAR _Reserved_for_largest_union_variant[1536];
    } QueryResult;
};

#pragma pack(pop)

using PFN_D3DKMTQueryStatistics =
    LONG (APIENTRY *)(B70_D3DKMT_QUERYSTATISTICS*);

}
