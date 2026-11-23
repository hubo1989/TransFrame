#pragma once
// EngineSelectionV2.h — converged ordered/compare engine selection
// (macOS EngineSelectionV2.swift). JSON shape (observed):
//   {"id":"EE86…","kind":{"builtin":{"_0":"openai"}}}
//   {"id":"5A3A…","kind":{"compatible":{"_0":"1234…"}}}

#include "Json.h"
#include "TranslationEngineType.h"
#include "Uuid.h"

#include <optional>
#include <string>
#include <vector>

namespace tf::core {

/// One entry in the ordered chain or compare set.
struct EngineChainItem {
    Uuid id;
    struct Kind {
        enum class Tag { Builtin, Compatible };
        Tag tag = Tag::Builtin;
        TranslationEngineType engine = TranslationEngineType::MtranServer;  // when Builtin
        Uuid instanceId;                                                     // when Compatible

        friend bool operator==(const Kind& a, const Kind& b) {
            if (a.tag != b.tag) { return false; }
            if (a.tag == Tag::Builtin) { return a.engine == b.engine; }
            return a.instanceId == b.instanceId;
        }
    } kind;

    EngineChainItem() = default;
    static EngineChainItem builtin(TranslationEngineType e) {
        EngineChainItem item;
        item.id = Uuid::randomUuid();
        item.kind = Kind{Kind::Tag::Builtin, e, {}};
        return item;
    }
    static EngineChainItem compatible(Uuid instance) {
        EngineChainItem item;
        item.id = Uuid::randomUuid();
        item.kind = Kind{Kind::Tag::Compatible, TranslationEngineType::Custom, instance};
        return item;
    }

    /// The builtin engine this entry reports as (compatible instances surface
    /// as Custom; UI labels use the instance display name).
    TranslationEngineType resolvedEngine() const {
        return kind.tag == Kind::Tag::Builtin ? kind.engine : TranslationEngineType::Custom;
    }

    friend bool operator==(const EngineChainItem& a, const EngineChainItem& b) {
        return a.id == b.id && a.kind == b.kind;
    }

    void toJson(Json& j) const {
        Json kindJson;
        if (kind.tag == Kind::Tag::Builtin) {
            kindJson["builtin"]["_0"] = toRaw(kind.engine);
        } else {
            kindJson["compatible"]["_0"] = kind.instanceId;
        }
        j = Json{{"id", id}, {"kind", std::move(kindJson)}};
    }
    static EngineChainItem fromJson(const Json& j) {
        EngineChainItem item;
        item.id = j.at("id").get<Uuid>();
        const Json& kind = j.at("kind");
        if (kind.contains("builtin")) {
            const std::string raw = kind.at("builtin").at("_0").get<std::string>();
            const TranslationEngineType e = fromRaw(TranslationEngineType{}, raw);
            if (e == TranslationEngineType::Invalid) {
                throw std::invalid_argument("unknown builtin engine: " + raw);
            }
            item.kind = Kind{Kind::Tag::Builtin, e, {}};
        } else if (kind.contains("compatible")) {
            item.kind = Kind{Kind::Tag::Compatible, TranslationEngineType::Custom,
                             kind.at("compatible").at("_0").get<Uuid>()};
        } else {
            throw std::invalid_argument("EngineChainItem.Kind requires builtin or compatible");
        }
        return item;
    }
};

inline void to_json(Json& j, const EngineChainItem& item) { item.toJson(j); }
inline void from_json(const Json& j, EngineChainItem& item) { item = EngineChainItem::fromJson(j); }

/// Converged selection modes.
enum class EngineModeV2 : int32_t { Ordered, Compare };
inline std::string toRaw(EngineModeV2 m) { return m == EngineModeV2::Ordered ? "ordered" : "compare"; }
inline EngineModeV2 engineModeFromRaw(const std::string& raw) {
    return raw == "compare" ? EngineModeV2::Compare : EngineModeV2::Ordered;
}

/// Structural limits (EngineSelectionLimits.swift).
struct EngineSelectionLimits {
    static constexpr int kOrderedChainLimit = 5;
    static constexpr int kCompareEnginesLimit = 4;
    static constexpr int kCompareEnginesMinimum = 2;
};

}  // namespace tf::core
