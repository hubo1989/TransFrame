#pragma once
// PerformanceRecorder.h — FR-40 performance instrumentation (port of the
// macOS PerformanceRecorder.swift actor). Portable Core module: no Win32.
//
// Semantics (mirrors the Swift actor):
//   * every pipeline run carries one operationID (UUID string) that links all
//     of its stage metrics;
//   * stages = displayLookup / capture / ocr / grouping / translation / render;
//   * durations accumulate in milliseconds; failures carry an error category.
// The Windows side swaps os_signpost for a thread-safe in-memory recorder
// with a JSON report export (the settings page wires an entry point later).
//
// Thread model: the App records capture/displayLookup on the main thread and
// ocr/translation/render on worker threads; every method is mutex-guarded.

#include "Json.h"
#include "Uuid.h"

#include <chrono>
#include <cstdint>
#include <map>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace tf::core {

/// Pipeline stages (Swift PerformanceStage; Windows set per PRD FR-40).
enum class PerfStage : int32_t {
    DisplayLookup = 0,
    Capture,
    Ocr,
    Grouping,
    Translation,
    Render,
};

const char* toRaw(PerfStage stage);
/// Inverse of toRaw; empty result for unknown strings.
std::optional<PerfStage> perfStageFromRaw(const std::string& raw);

/// One measured stage (Swift PerformanceMetric).
struct PerfMetric {
    PerfStage stage = PerfStage::Capture;
    std::string operationId;               // links all stages of one run
    double durationMs = 0.0;
    bool success = true;
    std::string errorCategory;             // empty on success
    std::int64_t timestampMs = 0;          // wall clock, epoch ms

    friend bool operator==(const PerfMetric&, const PerfMetric&) = default;
};

/// Aggregation over the recorded metrics of one stage (Swift
/// PerformanceSummary — count/total/avg/max plus a failure count).
struct PerfStageSummary {
    PerfStage stage = PerfStage::Capture;
    std::int64_t count = 0;
    std::int64_t failures = 0;
    double totalMs = 0.0;
    double avgMs = 0.0;
    double maxMs = 0.0;
};

/// Per-operation rollup for the report (one pipeline run).
struct PerfOperationSummary {
    std::string operationId;
    double totalMs = 0.0;                  // sum of recorded stage durations
    std::int64_t stageCount = 0;
    bool failed = false;                   // any stage recorded a failure
};

/// Thread-safe recorder (Swift PerformanceRecorder.shared). Constructible
/// so tests can exercise isolated instances; the App uses shared().
class PerformanceRecorder {
public:
    PerformanceRecorder() = default;

    static PerformanceRecorder& shared();

    /// Append one metric. Cheap; never blocks the pipeline on I/O.
    void record(const PerfMetric& metric);

    /// Summaries per stage, ordered by PerfStage value.
    std::vector<PerfStageSummary> summaries() const;

    /// Per-operation rollups, newest first.
    std::vector<PerfOperationSummary> operations() const;

    /// All recorded metrics (snapshot copy).
    std::vector<PerfMetric> metrics() const;

    /// Report document (macOS export parity): generatedAt + stages +
    /// operations. Shape is stable for the settings-page export entry.
    Json reportJson() const;

    /// Report as a serialized JSON string (2-space indent).
    std::string reportJsonString() const;

    void reset();

    /// Number of retained metrics (bounded; oldest dropped first).
    std::size_t size() const;

private:
    static constexpr std::size_t kMaxMetrics = 2000;

    mutable std::mutex mutex_;
    std::vector<PerfMetric> metrics_;
};

/// RAII stage timer: records success on destruction, or the given error
/// category when fail() was called first (exception-neutral like the Swift
/// measure(stage:operationID:) helper).
class PerfScope {
public:
    PerfScope(PerfStage stage, std::string operationId,
              PerformanceRecorder& recorder = PerformanceRecorder::shared());
    ~PerfScope();

    PerfScope(const PerfScope&) = delete;
    PerfScope& operator=(const PerfScope&) = delete;

    /// Mark the measured operation failed with a category; the destructor
    /// still records the elapsed duration.
    void fail(std::string category);

    /// Skip recording entirely (e.g. the stage turned out not to apply).
    void disarm();

private:
    PerformanceRecorder* recorder_;
    PerfStage stage_;
    std::string operationId_;
    std::string errorCategory_;
    bool success_ = true;
    bool armed_ = true;
    std::chrono::steady_clock::time_point startedAt_;
};

}  // namespace tf::core
