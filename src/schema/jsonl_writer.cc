#include "schema/jsonl_writer.h"

#include "schema/compact_format.h"
#include "schema/json_emit.h"

#include <cerrno>
#include <chrono>
#include <cstring>
#include <fstream>

namespace b70 {

JsonlWriter::JsonlWriter() = default;

JsonlWriter::~JsonlWriter() { close(); }

bool JsonlWriter::open(const std::filesystem::path& jsonl_path,
                       const std::filesystem::path& schema_sidecar_path) {
    if (file_) close();
    std::error_code ec;
    std::filesystem::create_directories(jsonl_path.parent_path(), ec);
    file_ = std::fopen(jsonl_path.string().c_str(), "wb");
    if (!file_) return false;
    std::setvbuf(file_, nullptr, _IOFBF, 64 * 1024);

    std::ofstream sidecar(schema_sidecar_path, std::ios::binary | std::ios::trunc);
    if (!sidecar) return false;
    sidecar << compact::schema_sidecar_json();
    sidecar.close();
    return true;
}

void JsonlWriter::close() {
    if (file_) {
        flush();
        std::fclose(file_);
        file_ = nullptr;
    }
}

void JsonlWriter::flush() {
    if (!file_) return;
    const auto t0 = std::chrono::steady_clock::now();
    std::fflush(file_);
    const auto t1 = std::chrono::steady_clock::now();
    const auto ns = static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    ++flush_count_;
    flush_total_ns_ += ns;
    flush_max_ns_ = std::max(flush_max_ns_, ns);
}

void JsonlWriter::write_line(std::string_view sv) {
    if (!file_ || sv.empty()) return;
    std::fwrite(sv.data(), 1, sv.size(), file_);
    ++lines_written_;
}

void JsonlWriter::on_metric(const MetricSample& m) {
    if (!file_) return;
    if (!delta_.should_emit(m, m.timestamp_qpc)) return;
    json_emit::LineBuilder lb;
    bool nan_seen = false, inf_seen = false;
    compact::encode_metric(lb, m, nan_seen, inf_seen);
    if (!lb.overflowed()) write_line(lb.view());
}

void JsonlWriter::on_identity(const AdapterIdentity& a) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_identity(lb, a);
    if (!lb.overflowed()) write_line(lb.view());
}

void JsonlWriter::on_state_transition(const AdapterStateTransition& t) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_state_transition(lb, t);
    if (!lb.overflowed()) write_line(lb.view());
}

void JsonlWriter::on_disagreement(const DisagreementReport& d) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_disagreement(lb, d);
    if (!lb.overflowed()) write_line(lb.view());
}

void JsonlWriter::on_epoch_boundary(const SessionEpochBoundary& e) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_epoch_boundary(lb, e);
    if (!lb.overflowed()) write_line(lb.view());
    delta_.clear();
}

void JsonlWriter::on_fingerprint(const DriverRuntimeFingerprint& f) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_fingerprint(lb, f);
    if (!lb.overflowed()) write_line(lb.view());
}

void JsonlWriter::on_audit(const CollectorAuditRecord& c) {
    if (!file_) return;
    json_emit::LineBuilder lb;
    compact::encode_audit(lb, c);
    if (!lb.overflowed()) write_line(lb.view());
}

}
