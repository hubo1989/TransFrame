import Foundation
import os
import os.signpost
import Darwin

actor PerformanceRecorder {
    static let shared = PerformanceRecorder()

    private let log = OSLog(
        subsystem: Bundle.main.bundleIdentifier ?? "TransFrame",
        category: "PerformancePipeline"
    )
    private var metricsByStage: [PerformanceStage: [PerformanceMetric]] = [:]

    func record(_ metric: PerformanceMetric) {
        metricsByStage[metric.stage, default: []].append(metric)
    }

    func measure<T: Sendable>(
        stage: PerformanceStage,
        operationID: UUID,
        operation: @Sendable () async throws -> T
    ) async throws -> T {
        let signpostID = OSSignpostID(log: log)
        os_signpost(.begin, log: log, name: "PipelineStage", signpostID: signpostID, "%{public}s", stage.rawValue)
        let startedAt = ContinuousClock.now

        do {
            let startMemory = Self.currentMemoryUsageBytes()
            let value = try await operation()
            let endMemory = Self.currentMemoryUsageBytes()
            let duration = startedAt.duration(to: .now).timeInterval
            os_signpost(.end, log: log, name: "PipelineStage", signpostID: signpostID, "%{public}s", stage.rawValue)
            record(
                PerformanceMetric(
                    stage: stage,
                    operationID: operationID,
                    duration: duration,
                    memoryDeltaBytes: endMemory - startMemory,
                    success: true,
                    errorCategory: nil
                )
            )
            return value
        } catch let cancellationError as CancellationError {
            let duration = startedAt.duration(to: .now).timeInterval
            os_signpost(.end, log: log, name: "PipelineStage", signpostID: signpostID, "%{public}s", stage.rawValue)
            record(
                PerformanceMetric(
                    stage: stage,
                    operationID: operationID,
                    duration: duration,
                    memoryDeltaBytes: 0,
                    success: false,
                    errorCategory: .cancelled
                )
            )
            throw cancellationError
        } catch let caughtError {
            let duration = startedAt.duration(to: .now).timeInterval
            os_signpost(.end, log: log, name: "PipelineStage", signpostID: signpostID, "%{public}s", stage.rawValue)
            let errorCategory = (caughtError as? PipelineError)?.category ?? .unknown
            record(
                PerformanceMetric(
                    stage: stage,
                    operationID: operationID,
                    duration: duration,
                    memoryDeltaBytes: 0,
                    success: false,
                    errorCategory: errorCategory
                )
            )
            throw caughtError
        }
    }

    func summaries() -> [PerformanceStage: PerformanceSummary] {
        metricsByStage.reduce(into: [:]) { result, entry in
            result[entry.key] = PerformanceSummary(stage: entry.key, metrics: entry.value)
        }
    }

    func reset() {
        metricsByStage.removeAll()
    }

    private static func currentMemoryUsageBytes() -> Int64 {
        var info = task_vm_info_data_t()
        var count = mach_msg_type_number_t(MemoryLayout<task_vm_info_data_t>.size / MemoryLayout<natural_t>.size)
        let kerr = withUnsafeMutablePointer(to: &info) {
            $0.withMemoryRebound(to: integer_t.self, capacity: Int(count)) {
                task_info(mach_task_self_, task_flavor_t(TASK_VM_INFO), $0, &count)
            }
        }
        if kerr == KERN_SUCCESS {
            return Int64(info.phys_footprint)
        }
        return 0
    }
}

private extension Duration {
    var timeInterval: TimeInterval {
        let durationComponents = self.components
        return TimeInterval(durationComponents.seconds)
            + TimeInterval(durationComponents.attoseconds) / 1_000_000_000_000_000_000
    }
}
