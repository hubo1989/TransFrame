import Foundation
import CoreGraphics
import AppKit
import CryptoKit
import Security

/// Supported image hosting providers.
enum ImageHostType: String, CaseIterable, Codable, Sendable {
    case none = "none"
    case picgo = "picgo"
    case aliyunOSS = "aliyunOSS"
    case tencentCOS = "tencentCOS"
    case qiniuKodo = "qiniuKodo"

    var displayName: String {
        switch self {
        case .none: return "None (Disabled)"
        case .picgo: return "PicGo (Local App)"
        case .aliyunOSS: return "Aliyun OSS"
        case .tencentCOS: return "Tencent Cloud COS"
        case .qiniuKodo: return "Qiniu Kodo"
        }
    }
}

/// Image Host configuration stored locally in AppSettings / UserDefaults.
struct ImageHostSettings: Codable, Sendable, Equatable {
    var activeHost: ImageHostType = .none
    var picgoEndpoint: String = "http://127.0.0.1:36677/upload"
    var copyMarkdownFormat: Bool = true

    // Aliyun OSS
    var ossBucket: String = ""
    var ossEndpoint: String = "" // e.g. oss-cn-hangzhou.aliyuncs.com
    var ossAccessKeyId: String = ""
    var ossAccessKeySecret: String = ""
    var ossPathPrefix: String = "transframe/"

    // Tencent COS
    var cosBucket: String = ""
    var cosRegion: String = "" // e.g. ap-guangzhou
    var cosSecretId: String = ""
    var cosSecretKey: String = ""
    var cosPathPrefix: String = "transframe/"

    // Qiniu Kodo
    var kodoBucket: String = ""
    var kodoDomain: String = "" // e.g. https://cdn.example.com
    var kodoAccessKey: String = ""
    var kodoSecretKey: String = ""
    var kodoPathPrefix: String = "transframe/"

    static let `default` = ImageHostSettings()
}

extension Notification.Name {
    static let imageDidUpload = Notification.Name("imageDidUpload")
    static let colorDidPick = Notification.Name("colorDidPick")
}

/// Upload result representation
struct UploadResult: Sendable {
    let url: String
    let markdown: String
    let provider: ImageHostType
}

/// Keychain storage for image-host credentials. UserDefaults keeps only the
/// non-sensitive parts of ImageHostSettings — plaintext secrets there are
/// readable by any process on the machine and travel with system backups.
private enum ImageHostSecretStore {
    private static let service = "TransFrame.ImageHostSettings"

    static func save(_ value: String, account: String) {
        let data = Data(value.utf8)
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
        ]
        let status = SecItemUpdate(query as CFDictionary, [kSecValueData as String: data] as CFDictionary)
        if status == errSecItemNotFound {
            var add = query
            add[kSecValueData as String] = data
            SecItemAdd(add as CFDictionary, nil)
        }
    }

    static func read(account: String) -> String? {
        let query: [String: Any] = [
            kSecClass as String: kSecClassGenericPassword,
            kSecAttrService as String: service,
            kSecAttrAccount as String: account,
            kSecReturnData as String: true,
            kSecMatchLimit as String: kSecMatchLimitOne,
        ]
        var item: CFTypeRef?
        guard SecItemCopyMatching(query as CFDictionary, &item) == errSecSuccess,
              let data = item as? Data else {
            return nil
        }
        return String(data: data, encoding: .utf8)
    }
}

/// Service that handles uploading screenshot image data directly to cloud providers or PicGo.
@MainActor
final class ImageUploadService {
    static let shared = ImageUploadService()

    private let userDefaultsKey = "TransFrame_ImageHostSettings"

    /// Keychain accounts for the three provider secrets.
    private static let secretAccounts = ("ossAccessKeySecret", "cosSecretKey", "kodoSecretKey")

    var settings: ImageHostSettings {
        didSet {
            saveSettings()
        }
    }

    private init() {
        var loaded: ImageHostSettings
        if let data = UserDefaults.standard.data(forKey: userDefaultsKey),
           let decoded = try? JSONDecoder().decode(ImageHostSettings.self, from: data) {
            loaded = decoded
        } else {
            loaded = .default
        }

        // Secrets live in Keychain; migrate legacy plaintext (from older
        // builds that stored the whole struct in UserDefaults) exactly once.
        let (ossAccount, cosAccount, kodoAccount) = Self.secretAccounts

        if let stored = ImageHostSecretStore.read(account: ossAccount), !stored.isEmpty {
            loaded.ossAccessKeySecret = stored
        } else if !loaded.ossAccessKeySecret.isEmpty {
            ImageHostSecretStore.save(loaded.ossAccessKeySecret, account: ossAccount)
        }
        if let stored = ImageHostSecretStore.read(account: cosAccount), !stored.isEmpty {
            loaded.cosSecretKey = stored
        } else if !loaded.cosSecretKey.isEmpty {
            ImageHostSecretStore.save(loaded.cosSecretKey, account: cosAccount)
        }
        if let stored = ImageHostSecretStore.read(account: kodoAccount), !stored.isEmpty {
            loaded.kodoSecretKey = stored
        } else if !loaded.kodoSecretKey.isEmpty {
            ImageHostSecretStore.save(loaded.kodoSecretKey, account: kodoAccount)
        }

        self.settings = loaded
        // Persist a sanitized JSON (secrets blanked) — also clears any legacy
        // plaintext that older builds left in UserDefaults. Idempotent.
        saveSettings()
    }

    private func saveSettings() {
        let (ossAccount, cosAccount, kodoAccount) = Self.secretAccounts
        ImageHostSecretStore.save(settings.ossAccessKeySecret, account: ossAccount)
        ImageHostSecretStore.save(settings.cosSecretKey, account: cosAccount)
        ImageHostSecretStore.save(settings.kodoSecretKey, account: kodoAccount)

        var sanitized = settings
        sanitized.ossAccessKeySecret = ""
        sanitized.cosSecretKey = ""
        sanitized.kodoSecretKey = ""
        if let encoded = try? JSONEncoder().encode(sanitized) {
            UserDefaults.standard.set(encoded, forKey: userDefaultsKey)
        }
    }

    var isConfigured: Bool {
        settings.activeHost != .none
    }

    /// Uploads a CGImage using the currently active image host.
    func upload(image: CGImage) async throws -> UploadResult {
        guard settings.activeHost != .none else {
            throw TransFrameError.imageUploadFailed(reason: "no image host configured")
        }

        // Convert CGImage to PNG Data
        let rep = NSBitmapImageRep(cgImage: image)
        guard let data = rep.representation(using: .png, properties: [:]) else {
            throw TransFrameError.exportEncodingFailed(format: .png)
        }

        let filename = "TF_\(Int(Date().timeIntervalSince1970))_\(UUID().uuidString.prefix(6)).png"
        let publicURLString: String

        switch settings.activeHost {
        case .none:
            throw TransFrameError.imageUploadFailed(reason: "no image host configured")

        case .picgo:
            publicURLString = try await uploadToPicGo(data: data, filename: filename)

        case .aliyunOSS:
            publicURLString = try await uploadToAliyunOSS(data: data, filename: filename)

        case .tencentCOS:
            publicURLString = try await uploadToTencentCOS(data: data, filename: filename)

        case .qiniuKodo:
            publicURLString = try await uploadToQiniuKodo(data: data, filename: filename)
        }

        let markdown = "![\(filename)](\(publicURLString))"
        let copiedText = settings.copyMarkdownFormat ? markdown : publicURLString

        // Copy to system clipboard
        NSPasteboard.general.clearContents()
        NSPasteboard.general.setString(copiedText, forType: .string)

        let result = UploadResult(url: publicURLString, markdown: markdown, provider: settings.activeHost)
        NotificationCenter.default.post(name: .imageDidUpload, object: result)

        return result
    }

    // MARK: - PicGo Local Upload

    private func uploadToPicGo(data: Data, filename: String) async throws -> String {
        guard let endpoint = URL(string: settings.picgoEndpoint) else {
            throw TransFrameError.imageUploadFailed(reason: "invalid PicGo endpoint")
        }

        let tempFile = FileManager.default.temporaryDirectory.appendingPathComponent(filename)
        try data.write(to: tempFile)
        defer { try? FileManager.default.removeItem(at: tempFile) }

        var request = URLRequest(url: endpoint)
        request.httpMethod = "POST"
        request.setValue("application/json", forHTTPHeaderField: "Content-Type")

        let payload: [String: Any] = ["list": [tempFile.path]]
        request.httpBody = try JSONSerialization.data(withJSONObject: payload)

        let (respData, response) = try await URLSession.shared.data(for: request)
        if let httpResponse = response as? HTTPURLResponse, (200...299).contains(httpResponse.statusCode) {
            if let json = try? JSONSerialization.jsonObject(with: respData) as? [String: Any],
               let resultList = json["result"] as? [String], let firstURL = resultList.first {
                return firstURL
            }
            throw TransFrameError.imageUploadFailed(reason: "unexpected PicGo response payload")
        }
        let code = (response as? HTTPURLResponse)?.statusCode ?? -1
        throw TransFrameError.imageUploadFailed(reason: "PicGo HTTP \(code)")
    }

    // MARK: - Aliyun OSS Upload (Direct REST PUT)

    private func uploadToAliyunOSS(data: Data, filename: String) async throws -> String {
        let bucket = settings.ossBucket
        let endpoint = settings.ossEndpoint.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        let keyId = settings.ossAccessKeyId
        let keySecret = settings.ossAccessKeySecret
        let keyPath = settings.ossPathPrefix.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/" + filename

        let host = "\(bucket).\(endpoint)"
        guard let url = URL(string: "https://\(host)/\(keyPath)") else {
            throw TransFrameError.imageUploadFailed(reason: "invalid OSS URL")
        }

        let rfc822Formatter = DateFormatter()
        rfc822Formatter.locale = Locale(identifier: "en_US_POSIX")
        rfc822Formatter.timeZone = TimeZone(secondsFromGMT: 0)
        rfc822Formatter.dateFormat = "EEE, dd MMM yyyy HH:mm:ss 'GMT'"
        let dateString = rfc822Formatter.string(from: Date())

        let contentType = "image/png"
        let canonicalizedResource = "/\(bucket)/\(keyPath)"
        let stringToSign = "PUT\n\n\(contentType)\n\(dateString)\n\(canonicalizedResource)"

        // HMAC-SHA1
        let key = SymmetricKey(data: Data(keySecret.utf8))
        let signatureData = HMAC<Insecure.SHA1>.authenticationCode(for: Data(stringToSign.utf8), using: key)
        let signature = Data(signatureData).base64EncodedString()
        let authHeader = "OSS \(keyId):\(signature)"

        var request = URLRequest(url: url)
        request.httpMethod = "PUT"
        request.setValue(host, forHTTPHeaderField: "Host")
        request.setValue(dateString, forHTTPHeaderField: "Date")
        request.setValue(contentType, forHTTPHeaderField: "Content-Type")
        request.setValue(authHeader, forHTTPHeaderField: "Authorization")
        request.httpBody = data

        let (_, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse, (200...299).contains(httpResponse.statusCode) else {
            let code = (response as? HTTPURLResponse)?.statusCode ?? -1
            throw TransFrameError.imageUploadFailed(reason: "OSS HTTP \(code)")
        }

        return "https://\(host)/\(keyPath)"
    }

    // MARK: - Tencent Cloud COS Upload (Direct REST PUT)

    private func uploadToTencentCOS(data: Data, filename: String) async throws -> String {
        let bucket = settings.cosBucket
        let region = settings.cosRegion
        let secretId = settings.cosSecretId
        let secretKey = settings.cosSecretKey
        let keyPath = settings.cosPathPrefix.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/" + filename

        let host = "\(bucket).cos.\(region).myqcloud.com"
        guard let url = URL(string: "https://\(host)/\(keyPath)") else {
            throw TransFrameError.imageUploadFailed(reason: "invalid COS URL")
        }

        let now = Int(Date().timeIntervalSince1970)
        let expire = now + 3600
        let qKeyTime = "\(now);\(expire)"

        // Q-Sign-Algorithm: sha1
        let signKey = HMAC<Insecure.SHA1>.authenticationCode(for: Data(qKeyTime.utf8), using: SymmetricKey(data: Data(secretKey.utf8)))
        let signKeyHex = signKey.map { String(format: "%02x", $0) }.joined()

        let httpMethod = "put"
        let uriPath = "/\(keyPath)"
        let formatString = "\(httpMethod)\n\(uriPath)\n\nhost=\(host)\n"
        let stringToSign = "sha1\n\(qKeyTime)\n\(Insecure.SHA1.hash(data: Data(formatString.utf8)).map { String(format: "%02x", $0) }.joined())\n"

        let signature = HMAC<Insecure.SHA1>.authenticationCode(for: Data(stringToSign.utf8), using: SymmetricKey(data: Data(signKeyHex.utf8)))
        let signatureHex = signature.map { String(format: "%02x", $0) }.joined()

        let authHeader = "q-sign-algorithm=sha1&q-ak=\(secretId)&q-sign-time=\(qKeyTime)&q-key-time=\(qKeyTime)&q-header-list=host&q-url-param-list=&q-signature=\(signatureHex)"

        var request = URLRequest(url: url)
        request.httpMethod = "PUT"
        request.setValue(host, forHTTPHeaderField: "Host")
        request.setValue(authHeader, forHTTPHeaderField: "Authorization")
        request.setValue("image/png", forHTTPHeaderField: "Content-Type")
        request.httpBody = data

        let (_, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse, (200...299).contains(httpResponse.statusCode) else {
            let code = (response as? HTTPURLResponse)?.statusCode ?? -1
            throw TransFrameError.imageUploadFailed(reason: "COS HTTP \(code)")
        }

        return "https://\(host)/\(keyPath)"
    }

    // MARK: - Qiniu Kodo Upload (POST Form)

    private func uploadToQiniuKodo(data: Data, filename: String) async throws -> String {
        let bucket = settings.kodoBucket
        let domain = settings.kodoDomain.trimmingCharacters(in: CharacterSet(charactersIn: "/"))
        let accessKey = settings.kodoAccessKey
        let secretKey = settings.kodoSecretKey
        let keyPath = settings.kodoPathPrefix.trimmingCharacters(in: CharacterSet(charactersIn: "/")) + "/" + filename

        // Generate Qiniu Upload Token (scope = bucket:key, deadline = now + 3600)
        let deadline = Int(Date().timeIntervalSince1970) + 3600
        let putPolicy: [String: Any] = ["scope": "\(bucket):\(keyPath)", "deadline": deadline]
        let policyData = try JSONSerialization.data(withJSONObject: putPolicy)
        let encodedPolicy = policyData.base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")

        let sign = HMAC<Insecure.SHA1>.authenticationCode(for: Data(encodedPolicy.utf8), using: SymmetricKey(data: Data(secretKey.utf8)))
        let encodedSign = Data(sign).base64EncodedString()
            .replacingOccurrences(of: "+", with: "-")
            .replacingOccurrences(of: "/", with: "_")

        let uploadToken = "\(accessKey):\(encodedSign):\(encodedPolicy)"

        // POST multipart form to https://upload.qiniup.com
        guard let uploadURL = URL(string: "https://upload.qiniup.com") else {
            throw TransFrameError.imageUploadFailed(reason: "invalid Kodo upload URL")
        }

        let boundary = "Boundary-\(UUID().uuidString)"
        var body = Data()

        // token field
        body.append("--\(boundary)\r\n".data(using: .utf8)!)
        body.append("Content-Disposition: form-data; name=\"token\"\r\n\r\n".data(using: .utf8)!)
        body.append("\(uploadToken)\r\n".data(using: .utf8)!)

        // key field
        body.append("--\(boundary)\r\n".data(using: .utf8)!)
        body.append("Content-Disposition: form-data; name=\"key\"\r\n\r\n".data(using: .utf8)!)
        body.append("\(keyPath)\r\n".data(using: .utf8)!)

        // file field
        body.append("--\(boundary)\r\n".data(using: .utf8)!)
        body.append("Content-Disposition: form-data; name=\"file\"; filename=\"\(filename)\"\r\n".data(using: .utf8)!)
        body.append("Content-Type: image/png\r\n\r\n".data(using: .utf8)!)
        body.append(data)
        body.append("\r\n".data(using: .utf8)!)

        body.append("--\(boundary)--\r\n".data(using: .utf8)!)

        var request = URLRequest(url: uploadURL)
        request.httpMethod = "POST"
        request.setValue("multipart/form-data; boundary=\(boundary)", forHTTPHeaderField: "Content-Type")
        request.httpBody = body

        let (_, response) = try await URLSession.shared.data(for: request)
        guard let httpResponse = response as? HTTPURLResponse, (200...299).contains(httpResponse.statusCode) else {
            let code = (response as? HTTPURLResponse)?.statusCode ?? -1
            throw TransFrameError.imageUploadFailed(reason: "Kodo HTTP \(code)")
        }

        let baseURL = domain.hasPrefix("http") ? domain : "https://\(domain)"
        return "\(baseURL)/\(keyPath)"
    }
}
