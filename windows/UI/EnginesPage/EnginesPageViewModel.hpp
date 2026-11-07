// windows/UI/EnginesPage/EnginesPageViewModel.hpp
// FR-35 引擎页签 ViewModel —— 纯 C++ / WinRT-free 头。
//
// 设计约定（与主线 shell 集成时的合同）：
//   * 本文件不包含任何 WinRT/UI 头 —— 可被 Core 单测直接包含、编译、测试。
//   * VM 是页面唯一的数据源：XAML code-behind (EnginesPage.xaml.h/.cpp) 把
//     本 VM 暴露为页面属性 `ViewModel()`，x:Bind 路径全部形如
//     `ViewModel.VlmProvider`、`ViewModel.ChainCountText` 等。
//   * VM 不重复定义模型：字段 1:1 包装 windows/Core/include/tf/ 的
//     EngineConfigStore::EngineConfig / VlmConfig / PaddleOcrConfig /
//     EngineChainEntry + CoreModels.hpp 的 TranslationEngineType 辅助函数 +
//     TranslationProvider.hpp 的 EngineConfig / CompatibleConfig。
//     字段名对齐 windows/docs/settings-config-schema.md 的 camelCase。
//   * 持久化协议（spec §7）：修改内存值后由调用方【显式】调用 Save*()，
//     不做"改内存即落盘"（mac @Observable didSet 不可靠的同一结论）。
//   * 每引擎配置（engineConfigs）与兼容实例（compatibleProviderConfigs）在
//     Core 的 EngineConfigStore 里由 wt-engines 分支持有，JSON 合并语义为
//     "未知键原样保留"；本 VM 通过 std::map/std::vector 内存镜像 + 序列化
//     回调与文件层解耦 —— 引擎管理分支合并后由集成方提供 load/save 闭包
//     （构造函数参数），未提供时 VM 工作在纯内存模式（页面可用、不落盘）。
//
// 线程模型：全部方法假定在 UI 线程（DispatcherQueue）调用；健康探测回调
// 从后台线程回来时由 code-behind 投递回 UI 线程再落 HealthStates。
#ifndef TF_UI_ENGINES_PAGE_VIEW_MODEL_HPP
#define TF_UI_ENGINES_PAGE_VIEW_MODEL_HPP

#include <tf/CoreModels.hpp>
#include <tf/EngineConfigStore.hpp>
#include <tf/EngineHealthChecker.hpp>
#include <tf/TranslationProvider.hpp>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tf::ui {

// ---------------------------------------------------------------------------
// 常量（spec §2.3）：链 1..5、对比集 2..4、兼容实例 0..5。
// ---------------------------------------------------------------------------
inline constexpr std::size_t kEngineChainMin = 1;
inline constexpr std::size_t kEngineChainLimit = 5;
inline constexpr std::size_t kCompareEnginesMinimum = 2;
inline constexpr std::size_t kCompareEnginesLimit = 4;
inline constexpr std::size_t kCompatibleInstancesLimit = 5;

/// 兼容实例 UI 行（模型本体 = tf::CompatibleConfig / CompatibleInstanceRow in cpp）。
struct CompatibleInstanceRow {
    std::string id;             // UUID string
    std::string displayName;
    std::string baseURL;
    std::string modelName;
    bool hasAPIKey = false;     // API Key 本体在 CredMan（按实例 id）
};

/// 链 / 对比集的一行 UI 数据（模型本体 = tf::core::EngineChainEntry）。
struct ChainRowViewModel {
    std::string id;             // entry id（同一引擎可重复入链）
    bool isCompatible = false;
    std::string builtinEngineRaw;   // "mtran" / "openai" / ... / "apple"
    std::string compatibleId;       // UUID string
    std::string displayName;        // resolvedEngine 显示名 / 兼容实例名
};

class EnginesPageViewModel {
public:
    /// 持久化钩子：集成方提供 engines.json 的整文件读写（Core 的
    /// parseEngineConfig / serializeEngineConfigPatch 已实现部分字段；
    /// engineConfigs / compatibleProviderConfigs 属 wt-engines 分支，其落盘
    /// 由集成方闭包完成 —— 纯内存模式下留空即可）。
    struct Persistence {
        std::function<std::string()> loadEnginesJson;             // 返回文件内容
        std::function<void(const std::string&)> saveEnginesJson;  // 原子写
    };

    explicit EnginesPageViewModel(Persistence persistence = {});
    ~EnginesPageViewModel();

    EnginesPageViewModel(const EnginesPageViewModel&) = delete;
    EnginesPageViewModel& operator=(const EnginesPageViewModel&) = delete;

    // ------------------------------------------------------------------
    // 卡片 B：VLM 配置（schema vlm*，模型 = tf::core::VlmConfig）
    // ------------------------------------------------------------------
    // provider: "openai" | "claude" | "gemini" | "ollama" | "paddleocr"
    std::string VlmProvider() const { return vlm_.provider; }
    void SetVlmProvider(std::string_view v) { vlm_.provider = std::string(v); }
    bool IsValidVlmProvider(std::string_view v) const;

    std::string VlmBaseURL() const { return vlm_.baseURL; }
    void SetVlmBaseURL(std::string_view v) { vlm_.baseURL = std::string(v); }
    std::string VlmModelName() const { return vlm_.modelName; }
    void SetVlmModelName(std::string_view v) { vlm_.modelName = std::string(v); }
    // openAIMode: "chat" | "responses"，仅 provider=="openai" 显示。
    std::string VlmOpenAIMode() const { return vlm_.openAIMode; }
    void SetVlmOpenAIMode(std::string_view v) { vlm_.openAIMode = std::string(v); }
    bool VlmShowsOpenAIMode() const { return vlm_.provider == "openai"; }

    // ------------------------------------------------------------------
    // PaddleOCR（schema paddleOCR*，模型 = tf::core::PaddleOcrConfig）
    // ------------------------------------------------------------------
    // 状态行：已安装(版本号) / 未安装。版本号来自运行时探测注入。
    bool PaddleOcrInstalled() const { return !paddleVersionText_.empty(); }
    std::string PaddleOcrVersionText() const;
    void SetPaddleOcrVersionText(std::string_view v) { paddleVersionText_ = std::string(v); }

    std::string PaddleOcrMode() const { return paddle_.mode; }   // "fast"|"precise"
    void SetPaddleOcrMode(std::string_view v) { paddle_.mode = std::string(v); }
    bool PaddleOcrUseCloud() const { return paddle_.useCloud; }
    void SetPaddleOcrUseCloud(bool v) { paddle_.useCloud = v; }
    std::string PaddleOcrCloudBaseURL() const { return paddle_.cloudBaseURL; }
    void SetPaddleOcrCloudBaseURL(std::string_view v) { paddle_.cloudBaseURL = std::string(v); }
    std::string PaddleOcrCloudModelId() const { return paddle_.cloudModelId; }
    void SetPaddleOcrCloudModelId(std::string_view v) { paddle_.cloudModelId = std::string(v); }
    // 云端 API Key 本体在 CredMan（schema 注：mac 走 Keychain）——VM 只持
    // hasKey 布尔，SecureField 读写经 SetPaddleOcrCloudAPIKey 钩子（集成方
    // 落 CredMan；纯内存模式下仅置位）。
    bool PaddleOcrCloudHasAPIKey() const { return paddleCloudHasApiKey_; }
    void SetPaddleOcrCloudAPIKey(std::string_view key);

    // ------------------------------------------------------------------
    // 卡片 C-1：策略（schema engineModeV2）
    // ------------------------------------------------------------------
    std::string EngineModeV2() const { return config_.engineModeV2; }
    void SetEngineModeV2(std::string_view v) { config_.engineModeV2 = std::string(v); }
    bool IsOrderedMode() const { return config_.engineModeV2 == "ordered"; }
    bool IsCompareMode() const { return config_.engineModeV2 == "compare"; }

    // ------------------------------------------------------------------
    // C-2 顺序回退链（schema engineChainV2）
    // ------------------------------------------------------------------
    std::vector<ChainRowViewModel> ChainRows() const;
    std::string ChainCountText() const;       // "n/5"
    bool CanAddChainRow() const;
    bool CanRemoveChainRow() const;
    // 候选项：所有 isEnabled 的内置引擎（含 apple；排除 .custom）+ 全部兼容实例。
    std::vector<ChainRowViewModel> ChainCandidates() const;
    void AppendChainRow(const ChainRowViewModel& row);         // id 为空时生成
    void ReplaceChainRow(std::size_t index, const ChainRowViewModel& row);
    void RemoveChainRow(std::size_t index);
    void MoveChainRow(std::size_t from, std::size_t to);       // 拖拽排序

    // ------------------------------------------------------------------
    // C-3 并行对比（schema compareEnginesV2）
    // ------------------------------------------------------------------
    std::vector<ChainRowViewModel> CompareRows() const;
    std::string CompareCountText() const;     // "n/4"
    bool CanAddCompareRow() const;
    bool CanRemoveCompareRow() const;
    std::vector<ChainRowViewModel> CompareCandidates() const;
    void AppendCompareRow(const ChainRowViewModel& row);
    void RemoveCompareRow(std::size_t index);

    // ------------------------------------------------------------------
    // C-4 可用引擎卡（schema engineConfigs / compatibleProviderConfigs）
    // ------------------------------------------------------------------
    // 9 内置卡片（mac uiCards 顺序；apple→mtran→LLM→云服务，PRD §6.12：
    // Windows 端 Apple Translation 移除后按 CoreModels 排列）：
    // mtran, openai, claude, gemini, ollama, google, deepl, baidu, custom
    std::vector<std::string> BuiltinCardEngineRaws() const;
    bool BuiltinEnabled(std::string_view engineRaw) const;
    void SetBuiltinEnabled(std::string_view engineRaw, bool enabled);
    // custom 卡片不进选择列表（spec：添加 Menu 排除 .custom）。
    bool BuiltinSelectable(std::string_view engineRaw) const;

    std::vector<CompatibleInstanceRow> CompatibleInstances() const { return compatible_; }
    std::string CompatibleCountText() const;  // "n/5"
    bool CanAddCompatibleInstance() const;
    bool CompatibleLimitReached() const { return !CanAddCompatibleInstance(); }
    void AppendCompatibleInstance(const CompatibleInstanceRow& row);
    void RemoveCompatibleInstance(std::string_view id);
    CompatibleInstanceRow FindCompatible(std::string_view id) const;  // 未找到 → id 空
    // 该兼容实例是否被链或对比集引用（卡片副状态显示）。
    bool CompatibleInUse(std::string_view id) const;

    // ------------------------------------------------------------------
    // 健康状态（tf::EngineHealth 四态：绿/红/橙/灰；checking 是 UI 过渡态，
    // 由 code-behind 持有，不进 VM —— Core EngineHealthChecker.hpp 同款约定）。
    // ------------------------------------------------------------------
    // key: 内置引擎 raw 值；兼容实例: UUID。
    std::string HealthOf(std::string_view key) const;   // "green|red|orange|gray"
    void SetHealthOf(std::string_view key, std::string_view state);
    std::string HealthColorHex(std::string_view key) const;  // UI 状态点颜色

    // ------------------------------------------------------------------
    // Sheet 编辑工作副本（EngineConfigSheet / CompatibleEngineConfigSheet 的
    // "取消→回滚、保存→提交" 语义）：Sheet 打开时 Copy 当前配置为 draft，
    // 保存时提交回镜像并显式 Save；取消即丢弃 draft。
    // ------------------------------------------------------------------
    // Sheet 1 draft（模型 = tf::EngineConfig（per-engine））。
    tf::EngineConfig OpenBuiltinSheetDraft(std::string_view engineRaw) const;
    void CommitBuiltinSheet(const tf::EngineConfig& draft);

    // OpenAI 预设（spec §2.4）：openAI / ollamaLocal / custom。
    std::string OpenAIPreset() const { return openAIPreset_; }
    void SetOpenAIPreset(std::string_view v) { openAIPreset_ = std::string(v); }
    bool OpenAIShowsAPIKey() const;   // 预设联动：ollamaLocal / custom+本地URL 隐藏

    // Sheet 2 draft（兼容实例）。
    CompatibleInstanceRow OpenCompatibleSheetDraft(std::string_view id) const;
    CompatibleInstanceRow NewCompatibleSheetDraft() const;  // 空模板（新建）
    void CommitCompatibleSheet(const CompatibleInstanceRow& draft);

    // ------------------------------------------------------------------
    // 持久化（spec §7：显式调用；序列化用 Core 的
    // serializeEngineConfigPatch + 集成方 saveEnginesJson 闭包）。
    // ------------------------------------------------------------------
    bool SaveVlm();
    bool SavePaddleOcr();
    bool SaveEngineModeV2();
    bool SaveEngineChainV2();
    bool SaveCompareEnginesV2();
    bool SaveEngineConfigs();          // engineConfigs + compatibleProviderConfigs
    bool SaveAll();                    // 页面级"应用"（可选入口）

    // 测试/集成注入：直接读写内存镜像的 engineConfigs。
    const std::map<std::string, tf::EngineConfig>& EngineConfigMirror() const { return engineConfigs_; }
    void UpsertEngineConfig(const tf::EngineConfig& cfg);

private:
    std::string SerializeCurrent() const;   // 整文件 JSON（patch 序列化）
    ChainRowViewModel RowFor(const tf::core::EngineChainEntry& e) const;
    ChainRowViewModel CandidateFor(const std::string& raw) const;
    tf::core::EngineChainEntry EntryFrom(const ChainRowViewModel& row) const;

    tf::core::EngineConfig config_;          // engineModeV2 / 链 / 对比集 + vlm/paddle 镜像源
    tf::core::VlmConfig vlm_;
    tf::core::PaddleOcrConfig paddle_;
    std::string paddleVersionText_;
    bool paddleCloudHasApiKey_ = false;

    std::map<std::string, tf::EngineConfig> engineConfigs_;   // key = engine raw
    std::vector<CompatibleInstanceRow> compatible_;           // 兼容实例镜像
    std::map<std::string, std::string> health_;               // key -> green|red|orange|gray

    std::string openAIPreset_ = "openAI";
    Persistence persistence_;
};

}  // namespace tf::ui

#endif  // TF_UI_ENGINES_PAGE_VIEW_MODEL_HPP
