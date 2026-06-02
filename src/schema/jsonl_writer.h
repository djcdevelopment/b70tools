#pragma once

#include "bus/event_bus.h"
#include "schema/delta_filter.h"

#include <cstdio>
#include <filesystem>
#include <string>

namespace b70 {

class JsonlWriter : public EventSink {
public:
    JsonlWriter();
    ~JsonlWriter();

    bool open(const std::filesystem::path& jsonl_path,
              const std::filesystem::path& schema_sidecar_path);
    void close();
    void flush();

    DeltaFilter& delta_filter() { return delta_; }

    void on_metric(const MetricSample& m) override;
    void on_identity(const AdapterIdentity& a) override;
    void on_state_transition(const AdapterStateTransition& t) override;
    void on_disagreement(const DisagreementReport& d) override;
    void on_epoch_boundary(const SessionEpochBoundary& e) override;
    void on_fingerprint(const DriverRuntimeFingerprint& f) override;
    void on_audit(const CollectorAuditRecord& c) override;

    std::uint64_t lines_written() const { return lines_written_; }
    std::uint64_t flush_count() const { return flush_count_; }
    std::uint64_t flush_total_ns() const { return flush_total_ns_; }
    std::uint64_t flush_max_ns() const { return flush_max_ns_; }

private:
    std::FILE* file_ = nullptr;
    DeltaFilter delta_;
    std::uint64_t lines_written_ = 0;
    std::uint64_t flush_count_ = 0;
    std::uint64_t flush_total_ns_ = 0;
    std::uint64_t flush_max_ns_ = 0;

    void write_line(std::string_view sv);
};

}
