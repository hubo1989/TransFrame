// windows/UI/EnginesPage/EnginesPageViewModel.cpp
// FR-35 引擎页签 ViewModel 实现 —— 纯 C++，无 WinRT/UI 依赖（可进 Core 单测）。
#include "EnginesPageViewModel.hpp"

#include <algorithm>
#include <sstream>

namespace tf::ui {
namespace {

bool IEquals(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) return false;
    }
    return true;
}

bool IsLocalEndpoint(std::string_view url) {
    // mac: VLMConfig.isLocalEndpoint —— localhost / 127.0.0.1 / [::1] / host.docker.internal
    return url.find("localhost") != std::string_view::npos ||
           url.find("127.0.0.1") != std::string_view::npos ||
           url.find("[::1]") != std::string_view::npos ||
           url.find("host.docker.internal") != std::string_view::npos;
}

std::string EngineDisplay(const std::string& raw) {
    if (raw == "apple") return "Apple Translation";
    if (auto t = translationEngineFromRaw(raw)) return translationEngineDisplayName(*t);
    return raw;
}

}  // namespace

EnginesPageViewModel::EnginesPageViewModel(Persistence persistence)
    : persistence_(std::move(persistence)) {
    // 初始镜像：从 engines.json（若注入 load 闭包）解析；否则全默认。
    if (persistence_.loadEnginesJson) {
        auto r = core::parseEngineConfig(persistence_.loadEnginesJson());
        if (r.ok) {
            config_ = std::move(r.config);
            vlm_ = config_.vlm;
            paddle_ = config_.paddleOcr;
        }
    }
}

EnginesPageViewModel::~EnginesPageViewModel() = default;

// ---------------------------------------------------------------------------
// VLM
// ---------------------------------------------------------------------------
bool EnginesPageViewModel::IsValidVlmProvider(std::string_view v) const {
    return v == "openai" || v == "claude" || v == "gemini" ||
           v == "ollama" || v == "paddleocr";
}

// ---------------------------------------------------------------------------
// PaddleOCR
// ---------------------------------------------------------------------------
std::string EnginesPageViewModel::PaddleOcrVersionText() const {
    if (PaddleOcrInstalled()) return paddleVersionText_;
    return "未安装";  // UI 文案；code-behind 可按资源字典本地化
}

void EnginesPageViewModel::SetPaddleOcrCloudAPIKey(std::string_view key) {
    // 集成点：key 非空时写 CredMan（NativeShell CredManKeyStore，账户名见
    // CredentialAccounts.hpp 的 paddleOCRCloud 账户）；此处仅置位镜像。
    paddleCloudHasApiKey_ = !key.empty();
}

// ---------------------------------------------------------------------------
// 链 / 对比集
// ---------------------------------------------------------------------------
std::vector<ChainRowViewModel> EnginesPageViewModel::ChainRows() const {
    std::vector<ChainRowViewModel> rows;
    rows.reserve(config_.engineChainV2.size());
    for (const auto& e : config_.engineChainV2) rows.push_back(RowFor(e));
    return rows;
}

std::string EnginesPageViewModel::ChainCountText() const {
    return std::to_string(config_.engineChainV2.size()) + "/" +
           std::to_string(kEngineChainLimit);
}

bool EnginesPageViewModel::CanAddChainRow() const {
    return config_.engineChainV2.size() < kEngineChainLimit;
}
bool EnginesPageViewModel::CanRemoveChainRow() const {
    return config_.engineChainV2.size() > kEngineChainMin;
}

std::vector<ChainRowViewModel> EnginesPageViewModel::ChainCandidates() const {
    std::vector<ChainRowViewModel> out;
    // 内置：isEnabled（含 apple —— Core 默认仅 apple 启用）且排除 .custom。
    // apple 在 Windows 的 TranslationEngineType 里无枚举（PRD §6.12 移除），
    // 但 schema engineChainV2 仍以 "apple" raw 值持久化（mac 兼容），且默认
    // 链为 [apple]（EngineConfigStore.makeDefault）—— 候选列表保留 apple 行。
    if (BuiltinEnabled("apple")) out.push_back(CandidateFor("apple"));
    for (auto t : allTranslationEngines()) {
        std::string raw = translationEngineRaw(t);
        if (raw == "custom") continue;
        if (BuiltinEnabled(raw)) out.push_back(CandidateFor(raw));
    }
    for (const auto& c : compatible_) {
        ChainRowViewModel r;
        r.isCompatible = true;
        r.compatibleId = c.id;
        r.displayName = c.displayName;
        out.push_back(std::move(r));
    }
    return out;
}

void EnginesPageViewModel::AppendChainRow(const ChainRowViewModel& row) {
    if (!CanAddChainRow()) return;
    config_.engineChainV2.push_back(EntryFrom(row));
}

void EnginesPageViewModel::ReplaceChainRow(std::size_t index, const ChainRowViewModel& row) {
    if (index >= config_.engineChainV2.size()) return;
    // EngineChainEntry 无 id 字段（Core 存储模型）；行 id 是 UI 层身份，替换不迁移。
    config_.engineChainV2[index] = EntryFrom(row);
}

void EnginesPageViewModel::RemoveChainRow(std::size_t index) {
    if (index >= config_.engineChainV2.size()) return;
    config_.engineChainV2.erase(config_.engineChainV2.begin() + index);
}

void EnginesPageViewModel::MoveChainRow(std::size_t from, std::size_t to) {
    auto& v = config_.engineChainV2;
    if (from >= v.size() || to >= v.size() || from == to) return;
    auto item = v[from];
    v.erase(v.begin() + from);
    v.insert(v.begin() + to, std::move(item));
}

std::vector<ChainRowViewModel> EnginesPageViewModel::CompareRows() const {
    std::vector<ChainRowViewModel> rows;
    rows.reserve(config_.compareEnginesV2.size());
    for (const auto& e : config_.compareEnginesV2) rows.push_back(RowFor(e));
    return rows;
}

std::string EnginesPageViewModel::CompareCountText() const {
    return std::to_string(config_.compareEnginesV2.size()) + "/" +
           std::to_string(kCompareEnginesLimit);
}

bool EnginesPageViewModel::CanAddCompareRow() const {
    return config_.compareEnginesV2.size() < kCompareEnginesLimit;
}
bool EnginesPageViewModel::CanRemoveCompareRow() const {
    return config_.compareEnginesV2.size() > kCompareEnginesMinimum;
}

std::vector<ChainRowViewModel> EnginesPageViewModel::CompareCandidates() const {
    return ChainCandidates();
}

void EnginesPageViewModel::AppendCompareRow(const ChainRowViewModel& row) {
    if (!CanAddCompareRow()) return;
    config_.compareEnginesV2.push_back(EntryFrom(row));
}

void EnginesPageViewModel::RemoveCompareRow(std::size_t index) {
    if (index >= config_.compareEnginesV2.size()) return;
    config_.compareEnginesV2.erase(config_.compareEnginesV2.begin() + index);
}

// ---------------------------------------------------------------------------
// C-4 引擎卡
// ---------------------------------------------------------------------------
std::vector<std::string> EnginesPageViewModel::BuiltinCardEngineRaws() const {
    // CoreModels.allTranslationEngines() 的顺序 = mac uiCards minus apple：
    // mtran, openai, claude, gemini, ollama, google, deepl, baidu, custom
    std::vector<std::string> out;
    for (auto t : allTranslationEngines()) out.push_back(translationEngineRaw(t));
    return out;
}

bool EnginesPageViewModel::BuiltinEnabled(std::string_view engineRaw) const {
    if (engineRaw == "apple") {
        // apple 不在 TranslationEngineType（Windows 移除），但默认链默认启用它；
        // 内存镜像里没有条目 = 默认 true（schema：默认仅 apple isEnabled）。
        auto it = engineConfigs_.find(std::string(engineRaw));
        return it == engineConfigs_.end() || it->second.isEnabled;
    }
    auto t = translationEngineFromRaw(std::string(engineRaw));
    if (!t) return false;
    auto it = engineConfigs_.find(std::string(engineRaw));
    if (it != engineConfigs_.end()) return it->second.isEnabled;
    return false;  // 非 apple 默认未启用（mac default(for:)）
}

void EnginesPageViewModel::SetBuiltinEnabled(std::string_view engineRaw, bool enabled) {
    std::string key(engineRaw);
    auto it = engineConfigs_.find(key);
    if (it == engineConfigs_.end()) {
        tf::EngineConfig cfg;
        if (auto t = translationEngineFromRaw(key)) cfg.engine = *t;
        cfg.isEnabled = enabled;
        engineConfigs_.emplace(std::move(key), std::move(cfg));
    } else {
        it->second.isEnabled = enabled;
    }
}

bool EnginesPageViewModel::BuiltinSelectable(std::string_view engineRaw) const {
    return engineRaw != "custom";
}

std::string EnginesPageViewModel::CompatibleCountText() const {
    return std::to_string(compatible_.size()) + "/" +
           std::to_string(kCompatibleInstancesLimit);
}

bool EnginesPageViewModel::CanAddCompatibleInstance() const {
    return compatible_.size() < kCompatibleInstancesLimit;
}

void EnginesPageViewModel::AppendCompatibleInstance(const CompatibleInstanceRow& row) {
    if (!CanAddCompatibleInstance()) return;
    compatible_.push_back(row);
}

void EnginesPageViewModel::RemoveCompatibleInstance(std::string_view id) {
    compatible_.erase(
        std::remove_if(compatible_.begin(), compatible_.end(),
                       [&](const CompatibleInstanceRow& r) { return r.id == id; }),
        compatible_.end());
}

CompatibleInstanceRow EnginesPageViewModel::FindCompatible(std::string_view id) const {
    for (const auto& r : compatible_)
        if (r.id == id) return r;
    return {};
}

bool EnginesPageViewModel::CompatibleInUse(std::string_view id) const {
    auto used = [&](const tf::core::EngineChainEntry& e) {
        return e.isCompatible && e.compatibleId == id;
    };
    return std::any_of(config_.engineChainV2.begin(), config_.engineChainV2.end(), used) ||
           std::any_of(config_.compareEnginesV2.begin(), config_.compareEnginesV2.end(), used);
}

// ---------------------------------------------------------------------------
// 健康状态
// ---------------------------------------------------------------------------
std::string EnginesPageViewModel::HealthOf(std::string_view key) const {
    auto it = health_.find(std::string(key));
    return it == health_.end() ? std::string("gray") : it->second;
}

void EnginesPageViewModel::SetHealthOf(std::string_view key, std::string_view state) {
    health_[std::string(key)] = std::string(state);
}

std::string EnginesPageViewModel::HealthColorHex(std::string_view key) const {
    auto s = HealthOf(key);
    if (s == "green") return "#10893E";
    if (s == "red") return "#C42B1C";
    if (s == "orange") return "#FF8C00";
    return "#9E9E9E";  // gray / unknown
}

// ---------------------------------------------------------------------------
// Sheet drafts
// ---------------------------------------------------------------------------
tf::EngineConfig EnginesPageViewModel::OpenBuiltinSheetDraft(std::string_view engineRaw) const {
    auto it = engineConfigs_.find(std::string(engineRaw));
    if (it != engineConfigs_.end()) return it->second;
    tf::EngineConfig cfg;
    if (auto t = translationEngineFromRaw(std::string(engineRaw))) cfg = tf::EngineConfig::make(*t);
    return cfg;
}

void EnginesPageViewModel::CommitBuiltinSheet(const tf::EngineConfig& draft) {
    engineConfigs_[translationEngineRaw(draft.engine)] = draft;
}

bool EnginesPageViewModel::OpenAIShowsAPIKey() const {
    if (openAIPreset_ == "ollamaLocal") return false;
    if (openAIPreset_ == "custom" && IsLocalEndpoint(vlm_.baseURL)) return false;
    return true;
}

CompatibleInstanceRow EnginesPageViewModel::OpenCompatibleSheetDraft(std::string_view id) const {
    return FindCompatible(id);
}

CompatibleInstanceRow EnginesPageViewModel::NewCompatibleSheetDraft() const {
    // 新建模板（占位对齐 spec §2.5；id 由集成方生成 UUID 后回填）。
    CompatibleInstanceRow r;
    r.baseURL = "http://localhost:8000/v1";
    r.modelName = "gpt-4o-mini";
    return r;
}

void EnginesPageViewModel::CommitCompatibleSheet(const CompatibleInstanceRow& draft) {
    auto it = std::find_if(compatible_.begin(), compatible_.end(),
                           [&](const CompatibleInstanceRow& r) { return r.id == draft.id; });
    if (it != compatible_.end()) *it = draft;
    else AppendCompatibleInstance(draft);
}

// ---------------------------------------------------------------------------
// 持久化（显式保存；序列化 = serializeEngineConfigPatch + 集成方闭包）
// ---------------------------------------------------------------------------
std::string EnginesPageViewModel::SerializeCurrent() const {
    tf::core::EngineConfig c = config_;
    c.vlm = vlm_;
    c.paddleOcr = paddle_;
    return core::serializeEngineConfigPatch("", c);
}

bool EnginesPageViewModel::SaveVlm() {
    config_.vlm = vlm_;
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SavePaddleOcr() {
    config_.paddleOcr = paddle_;
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SaveEngineModeV2() {
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SaveEngineChainV2() {
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SaveCompareEnginesV2() {
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SaveEngineConfigs() {
    // engineConfigs / compatibleProviderConfigs 的 JSON 落盘属 wt-engines 分支
    // 的合并范围（Core EngineConfigStore 对其做"未知键保留"）；集成方闭包
    // 负责把它们与 SerializeCurrent() 的输出合并写回。
    if (!persistence_.saveEnginesJson) return false;
    persistence_.saveEnginesJson(SerializeCurrent());
    return true;
}

bool EnginesPageViewModel::SaveAll() {
    bool ok = SaveVlm();
    ok = SavePaddleOcr() && ok;
    ok = SaveEngineModeV2() && ok;
    ok = SaveEngineChainV2() && ok;
    ok = SaveCompareEnginesV2() && ok;
    ok = SaveEngineConfigs() && ok;
    return ok;
}

void EnginesPageViewModel::UpsertEngineConfig(const tf::EngineConfig& cfg) {
    engineConfigs_[translationEngineRaw(cfg.engine)] = cfg;
}

// ---------------------------------------------------------------------------
// 私有：行 <-> entry 映射
// ---------------------------------------------------------------------------
ChainRowViewModel EnginesPageViewModel::RowFor(const tf::core::EngineChainEntry& e) const {
    ChainRowViewModel r;
    r.isCompatible = e.isCompatible;
    if (e.isCompatible) {
        r.compatibleId = e.compatibleId;
        r.displayName = FindCompatible(e.compatibleId).displayName;
        if (r.displayName.empty()) r.displayName = "兼容引擎";
    } else {
        r.builtinEngineRaw = e.builtinEngine;
        r.displayName = EngineDisplay(e.builtinEngine);
    }
    return r;
}

ChainRowViewModel EnginesPageViewModel::CandidateFor(const std::string& raw) const {
    ChainRowViewModel r;
    r.builtinEngineRaw = raw;
    r.displayName = EngineDisplay(raw);
    return r;
}

tf::core::EngineChainEntry EnginesPageViewModel::EntryFrom(const ChainRowViewModel& row) const {
    tf::core::EngineChainEntry e;
    e.isCompatible = row.isCompatible;
    if (row.isCompatible) e.compatibleId = row.compatibleId;
    else e.builtinEngine = row.builtinEngineRaw;
    return e;
}

}  // namespace tf::ui
