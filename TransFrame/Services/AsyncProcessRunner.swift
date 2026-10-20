import Foundation

struct AsyncProcessOutput: Sendable {
    let terminationStatus: Int32
    let stdoutData: Data
    let stderrData: Data
}

final class AsyncProcessRunner: @unchecked Sendable {
    private let process: Process
    private let stdoutPipe: Pipe
    private let stderrPipe: Pipe
    private let lock = NSLock()
    private var isCancelled = false
    private var didResume = false
    private var stdoutBuffer = Data()
    private var stderrBuffer = Data()

    init(process: Process, stdoutPipe: Pipe = Pipe(), stderrPipe: Pipe = Pipe()) {
        self.process = process
        self.stdoutPipe = stdoutPipe
        self.stderrPipe = stderrPipe
        self.process.standardOutput = stdoutPipe
        self.process.standardError = stderrPipe
    }

    func run() async throws -> AsyncProcessOutput {
        try await withTaskCancellationHandler {
            try await withCheckedThrowingContinuation { continuation in
                stdoutPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
                    let data = handle.availableData
                    guard !data.isEmpty else { return }
                    self?.lock.withLock {
                        self?.stdoutBuffer.append(data)
                    }
                }

                stderrPipe.fileHandleForReading.readabilityHandler = { [weak self] handle in
                    let data = handle.availableData
                    guard !data.isEmpty else { return }
                    self?.lock.withLock {
                        self?.stderrBuffer.append(data)
                    }
                }

                process.terminationHandler = { [weak self] process in
                    self?.complete(process: process, continuation: continuation)
                }

                do {
                    try process.run()
                } catch {
                    stdoutPipe.fileHandleForReading.readabilityHandler = nil
                    stderrPipe.fileHandleForReading.readabilityHandler = nil
                    resumeOnce(continuation, result: .failure(error))
                }
            }
        } onCancel: {
            cancel()
        }
    }

    private func cancel() {
        lock.withLock {
            isCancelled = true
            if process.isRunning {
                process.terminate()
            }
        }
    }

    private func complete(
        process: Process,
        continuation: CheckedContinuation<AsyncProcessOutput, Error>
    ) {
        stdoutPipe.fileHandleForReading.readabilityHandler = nil
        stderrPipe.fileHandleForReading.readabilityHandler = nil

        let remainingStdout = stdoutPipe.fileHandleForReading.readDataToEndOfFile()
        let remainingStderr = stderrPipe.fileHandleForReading.readDataToEndOfFile()

        let (finalStdout, finalStderr, wasCancelled) = lock.withLock {
            if !remainingStdout.isEmpty {
                stdoutBuffer.append(remainingStdout)
            }
            if !remainingStderr.isEmpty {
                stderrBuffer.append(remainingStderr)
            }
            return (stdoutBuffer, stderrBuffer, isCancelled)
        }

        let output = AsyncProcessOutput(
            terminationStatus: process.terminationStatus,
            stdoutData: finalStdout,
            stderrData: finalStderr
        )

        if wasCancelled {
            resumeOnce(continuation, result: .failure(CancellationError()))
        } else {
            resumeOnce(continuation, result: .success(output))
        }
    }

    private func resumeOnce(
        _ continuation: CheckedContinuation<AsyncProcessOutput, Error>,
        result: Result<AsyncProcessOutput, Error>
    ) {
        let shouldResume = lock.withLock {
            guard !didResume else { return false }
            didResume = true
            return true
        }

        guard shouldResume else { return }

        switch result {
        case .success(let output):
            continuation.resume(returning: output)
        case .failure(let error):
            continuation.resume(throwing: error)
        }
    }
}
