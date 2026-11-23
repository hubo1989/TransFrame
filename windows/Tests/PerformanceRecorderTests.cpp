// PerformanceRecorderTests.cpp — FR-40: stage metrics, operation linkage,
// summaries and the JSON report shape (macOS PerformanceRecorder parity).
#include <catch2/catch_amalgamated.hpp>

#include "Core/PerformanceRecorder.h"

#include <chrono>
#include <thread>

using tf::core::PerfMetric;
using tf::core::PerfScope;
using tf::core::PerformanceRecorder;
using tf::core::PerfStage;
using tf::core::perfStageFromRaw;
using tf::core::toRaw;

namespace {

PerfMetric metric(PerfStage stage, const std::string& op, double ms, bool ok = true) {
    PerfMetric m;
    m.stage = stage;
    m.operationId = op;
    m.durationMs = ms;
    m.success = ok;
    m.timestampMs = 42;
    return m;
}

}  // namespace

TEST_CASE("perf stages round-trip through raw names", "[perf]") {
    CHECK(std::string(toRaw(PerfStage::DisplayLookup)) == "displayLookup");
    CHECK(std::string(toRaw(PerfStage::Capture)) == "capture");
    CHECK(std::string(toRaw(PerfStage::Ocr)) == "ocr");
    CHECK(std::string(toRaw(PerfStage::Grouping)) == "grouping");
    CHECK(std::string(toRaw(PerfStage::Translation)) == "translation");
    CHECK(std::string(toRaw(PerfStage::Render)) == "render");
    for (PerfStage s : {PerfStage::DisplayLookup, PerfStage::Capture, PerfStage::Ocr,
                        PerfStage::Grouping, PerfStage::Translation, PerfStage::Render}) {
        REQUIRE(perfStageFromRaw(toRaw(s)).has_value());
        CHECK(*perfStageFromRaw(toRaw(s)) == s);
    }
    CHECK_FALSE(perfStageFromRaw("nope").has_value());
}

TEST_CASE("recorder aggregates summaries per stage", "[perf]") {
    PerformanceRecorder rec;
    rec.record(metric(PerfStage::Ocr, "op1", 100.0));
    rec.record(metric(PerfStage::Ocr, "op2", 200.0));
    rec.record(metric(PerfStage::Ocr, "op3", 40.0, /*ok=*/false));
    rec.record(metric(PerfStage::Translation, "op1", 55.5));

    const auto sums = rec.summaries();
    REQUIRE(sums.size() == 2);
    // Ordered by stage value: Ocr (2) before Translation (4).
    CHECK(sums[0].stage == PerfStage::Ocr);
    CHECK(sums[0].count == 3);
    CHECK(sums[0].failures == 1);
    CHECK(sums[0].totalMs == Catch::Approx(340.0));
    CHECK(sums[0].avgMs == Catch::Approx(340.0 / 3.0));
    CHECK(sums[0].maxMs == Catch::Approx(200.0));
    CHECK(sums[1].stage == PerfStage::Translation);
    CHECK(sums[1].count == 1);
    CHECK(sums[1].totalMs == Catch::Approx(55.5));
}

TEST_CASE("operation rollup links stages by operationID", "[perf]") {
    PerformanceRecorder rec;
    rec.record(metric(PerfStage::Capture, "opA", 10.0));
    rec.record(metric(PerfStage::Ocr, "opA", 20.0));
    rec.record(metric(PerfStage::Translation, "opA", 30.0));
    rec.record(metric(PerfStage::Ocr, "opB", 5.0, /*ok=*/false));

    const auto ops = rec.operations();
    REQUIRE(ops.size() == 2);
    // Newest operation first.
    CHECK(ops[0].operationId == "opB");
    CHECK(ops[0].failed);
    CHECK(ops[0].stageCount == 1);
    CHECK(ops[1].operationId == "opA");
    CHECK_FALSE(ops[1].failed);
    CHECK(ops[1].stageCount == 3);
    CHECK(ops[1].totalMs == Catch::Approx(60.0));
}

TEST_CASE("report JSON carries stages and operations", "[perf]") {
    PerformanceRecorder rec;
    rec.record(metric(PerfStage::Render, "opX", 12.5));

    const tf::core::Json j = rec.reportJson();
    REQUIRE(j.contains("stages"));
    REQUIRE(j["stages"].is_array());
    REQUIRE(j["stages"].size() == 1);
    CHECK(j["stages"][0]["stage"].get<std::string>() == "render");
    CHECK(j["stages"][0]["count"].get<int>() == 1);
    CHECK(j["stages"][0]["totalMs"].get<double>() == Catch::Approx(12.5));
    REQUIRE(j.contains("operations"));
    REQUIRE(j["operations"].size() == 1);
    CHECK(j["operations"][0]["operationId"].get<std::string>() == "opX");
    CHECK_FALSE(j["operations"][0]["failed"].get<bool>());

    const std::string text = rec.reportJsonString();
    CHECK(text.find("\"operationId\"") != std::string::npos);
    CHECK(text.find("\"render\"") != std::string::npos);
}

TEST_CASE("PerfScope records elapsed duration and failure category", "[perf]") {
    PerformanceRecorder rec;
    {
        PerfScope scope(PerfStage::Capture, "opScope", rec);
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    {
        PerfScope scope(PerfStage::Translation, "opScope", rec);
        scope.fail("engineDown");
    }
    {
        PerfScope scope(PerfStage::Render, "opScope", rec);
        scope.disarm();
    }

    const auto metrics = rec.metrics();
    REQUIRE(metrics.size() == 2);
    CHECK(metrics[0].stage == PerfStage::Capture);
    CHECK(metrics[0].success);
    CHECK(metrics[0].durationMs >= 1.0);
    CHECK(metrics[0].operationId == "opScope");
    CHECK(metrics[1].stage == PerfStage::Translation);
    CHECK_FALSE(metrics[1].success);
    CHECK(metrics[1].errorCategory == "engineDown");
}

TEST_CASE("recorder bounds retained metrics", "[perf]") {
    PerformanceRecorder rec;
    for (int i = 0; i < 2100; ++i) {
        rec.record(metric(PerfStage::Ocr, "op" + std::to_string(i), 1.0));
    }
    CHECK(rec.size() == 2000);
    // Oldest dropped: the first retained metric is op100.
    const auto metrics = rec.metrics();
    CHECK(metrics.front().operationId == "op100");
    rec.reset();
    CHECK(rec.size() == 0);
    CHECK(rec.summaries().empty());
}

TEST_CASE("shared recorder is usable concurrently", "[perf][mt]") {
    PerformanceRecorder& rec = PerformanceRecorder::shared();
    rec.reset();
    std::thread t1([&] {
        for (int i = 0; i < 500; ++i) rec.record(metric(PerfStage::Ocr, "mt1", 1.0));
    });
    std::thread t2([&] {
        for (int i = 0; i < 500; ++i) rec.record(metric(PerfStage::Translation, "mt2", 1.0));
    });
    t1.join();
    t2.join();
    REQUIRE(rec.size() == 1000);
    const auto sums = rec.summaries();
    REQUIRE(sums.size() == 2);
    CHECK(sums[0].count == 500);
    CHECK(sums[1].count == 500);
}
