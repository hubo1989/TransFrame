// PerformanceRecorder.cpp — FR-40 performance instrumentation (see
// PerformanceRecorder.h). Pure Core: steady_clock durations, wall-clock
// timestamps, in-memory metrics + JSON report.
#include "PerformanceRecorder.h"

#include <algorithm>

namespace tf::core {

const char* toRaw(PerfStage stage) {
    switch (stage) {
        case PerfStage::DisplayLookup: return "displayLookup";
        case PerfStage::Capture: return "capture";
        case PerfStage::Ocr: return "ocr";
        case PerfStage::Grouping: return "grouping";
        case PerfStage::Translation: return "translation";
        case PerfStage::Render: return "render";
    }
    return "unknown";
}

std::optional<PerfStage> perfStageFromRaw(const std::string& raw) {
    static const std::pair<const char*, PerfStage> kMap[] = {
        {"displayLookup", PerfStage::DisplayLookup},
        {"capture", PerfStage::Capture},
        {"ocr", PerfStage::Ocr},
        {"grouping", PerfStage::Grouping},
        {"translation", PerfStage::Translation},
        {"render", PerfStage::Render},
    };
    for (const auto& [name, stage] : kMap) {
        if (raw == name) return stage;
    }
    return std::nullopt;
}

PerformanceRecorder& PerformanceRecorder::shared() {
    static PerformanceRecorder instance;
    return instance;
}

void PerformanceRecorder::record(const PerfMetric& metric) {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.push_back(metric);
    if (metrics_.size() > kMaxMetrics) {
        metrics_.erase(metrics_.begin(),
                       metrics_.begin() + static_cast<std::ptrdiff_t>(
                                              metrics_.size() - kMaxMetrics));
    }
}

std::vector<PerfStageSummary> PerformanceRecorder::summaries() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::map<int32_t, PerfStageSummary> byStage;
    for (const PerfMetric& m : metrics_) {
        PerfStageSummary& s = byStage[static_cast<int32_t>(m.stage)];
        s.stage = m.stage;
        ++s.count;
        if (!m.success) ++s.failures;
        s.totalMs += m.durationMs;
        s.maxMs = std::max(s.maxMs, m.durationMs);
    }
    std::vector<PerfStageSummary> out;
    out.reserve(byStage.size());
    for (auto& [_, s] : byStage) {
        s.avgMs = s.count > 0 ? s.totalMs / static_cast<double>(s.count) : 0.0;
        out.push_back(s);
    }
    return out;
}

std::vector<PerfOperationSummary> PerformanceRecorder::operations() const {
    std::lock_guard<std::mutex> lock(mutex_);
    // Preserve first-seen order of operation ids; roll up per id.
    std::vector<std::string> order;
    std::map<std::string, PerfOperationSummary> byId;
    for (const PerfMetric& m : metrics_) {
        auto it = byId.find(m.operationId);
        if (it == byId.end()) {
            order.push_back(m.operationId);
            it = byId.emplace(m.operationId, PerfOperationSummary{}).first;
            it->second.operationId = m.operationId;
        }
        it->second.totalMs += m.durationMs;
        ++it->second.stageCount;
        it->second.failed = it->second.failed || !m.success;
    }
    std::vector<PerfOperationSummary> out;
    out.reserve(order.size());
    // Newest operation first (report readers care about the latest run).
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        out.push_back(byId.at(*it));
    }
    return out;
}

std::vector<PerfMetric> PerformanceRecorder::metrics() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_;
}

Json PerformanceRecorder::reportJson() const {
    // Snapshot under one lock so stages/operations come from the same state.
    std::lock_guard<std::mutex> lock(mutex_);
    const std::vector<PerfStageSummary> stages = [&] {
        std::map<int32_t, PerfStageSummary> byStage;
        for (const PerfMetric& m : metrics_) {
            PerfStageSummary& s = byStage[static_cast<int32_t>(m.stage)];
            s.stage = m.stage;
            ++s.count;
            if (!m.success) ++s.failures;
            s.totalMs += m.durationMs;
            s.maxMs = std::max(s.maxMs, m.durationMs);
        }
        std::vector<PerfStageSummary> out;
        for (auto& [_, s] : byStage) {
            s.avgMs = s.count > 0 ? s.totalMs / static_cast<double>(s.count) : 0.0;
            out.push_back(s);
        }
        return out;
    }();

    Json stagesJson = Json::array();
    for (const PerfStageSummary& s : stages) {
        stagesJson.push_back(Json{
            {"stage", toRaw(s.stage)},
            {"count", s.count},
            {"failures", s.failures},
            {"totalMs", s.totalMs},
            {"avgMs", s.avgMs},
            {"maxMs", s.maxMs},
        });
    }

    Json operationsJson = Json::array();
    {
        std::vector<std::string> order;
        std::map<std::string, PerfOperationSummary> byId;
        for (const PerfMetric& m : metrics_) {
            auto it = byId.find(m.operationId);
            if (it == byId.end()) {
                order.push_back(m.operationId);
                it = byId.emplace(m.operationId, PerfOperationSummary{}).first;
                it->second.operationId = m.operationId;
            }
            it->second.totalMs += m.durationMs;
            ++it->second.stageCount;
            it->second.failed = it->second.failed || !m.success;
        }
        for (auto it = order.rbegin(); it != order.rend(); ++it) {
            const PerfOperationSummary& o = byId.at(*it);
            operationsJson.push_back(Json{
                {"operationId", o.operationId},
                {"totalMs", o.totalMs},
                {"stageCount", o.stageCount},
                {"failed", o.failed},
            });
        }
    }

    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::system_clock::now().time_since_epoch());
    return Json{
        {"version", 1},
        {"generatedAtMs", nowMs.count()},
        {"metricCount", metrics_.size()},
        {"stages", std::move(stagesJson)},
        {"operations", std::move(operationsJson)},
    };
}

std::string PerformanceRecorder::reportJsonString() const {
    return reportJson().dump(2);
}

void PerformanceRecorder::reset() {
    std::lock_guard<std::mutex> lock(mutex_);
    metrics_.clear();
}

std::size_t PerformanceRecorder::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return metrics_.size();
}

// ---------------------------------------------------------------- PerfScope ---

PerfScope::PerfScope(PerfStage stage, std::string operationId,
                     PerformanceRecorder& recorder)
    : recorder_(&recorder),
      stage_(stage),
      operationId_(std::move(operationId)),
      startedAt_(std::chrono::steady_clock::now()) {}

PerfScope::~PerfScope() {
    if (!armed_ || !recorder_) return;
    const double ms = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - startedAt_)
                          .count();
    PerfMetric m;
    m.stage = stage_;
    m.operationId = operationId_;
    m.durationMs = ms;
    m.success = success_;
    m.errorCategory = success_ ? std::string() : errorCategory_;
    m.timestampMs = std::chrono::duration_cast<std::chrono::milliseconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count();
    recorder_->record(m);
}

void PerfScope::fail(std::string category) {
    success_ = false;
    errorCategory_ = std::move(category);
}

void PerfScope::disarm() { armed_ = false; }

}  // namespace tf::core
