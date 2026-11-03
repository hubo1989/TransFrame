// AnnotationDocument.cpp — editing document + drag geometry (see header).
#include "AnnotationDocument.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace tf::core {

namespace {

/// UTF-8 aware code-point iteration for the text-bounds estimate.
template <typename F>
void forEachCodePoint(const std::string& s, F&& f) {
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = static_cast<unsigned char>(s[i]);
        std::size_t len = 1;
        if ((c & 0xE0) == 0xC0) { len = 2; }
        else if ((c & 0xF0) == 0xE0) { len = 3; }
        else if ((c & 0xF8) == 0xF0) { len = 4; }
        if (i + len > s.size()) { len = 1; }  // malformed tail: one byte
        const std::size_t start = i;
        i += len;
        f(start, len);
    }
}

bool isCjkCodePoint(const std::string& s, std::size_t start, std::size_t len) {
    // Decode enough to tell wide (CJK / fullwidth) from narrow glyphs. Only
    // the wide/narrow split matters for the width estimate.
    unsigned first = static_cast<unsigned char>(s[start]);
    std::uint32_t cp = 0;
    switch (len) {
        case 1: cp = first; break;
        case 2: cp = (first & 0x1F) << 6; break;
        case 3: cp = (first & 0x0F) << 12; break;
        default: cp = (first & 0x07) << 18; break;
    }
    for (std::size_t k = 1; k < len; ++k) {
        const unsigned cont = static_cast<unsigned char>(s[start + k]);
        cp |= (cont & 0x3F) << (6 * (len - 1 - k));
    }
    // CJK ideographs, kana, hangul syllables, fullwidth forms.
    return (cp >= 0x2E80 && cp <= 0x9FFF) || (cp >= 0xAC00 && cp <= 0xD7AF) ||
           (cp >= 0xFF01 && cp <= 0xFF60) || (cp >= 0x3000 && cp <= 0x303F);
}

double estimatedTextWidth(const TextAnnotation& t) {
    double width = 0.0;
    forEachCodePoint(t.content, [&](std::size_t start, std::size_t len) {
        width += isCjkCodePoint(t.content, start, len) ? t.style.fontSize
                                                       : t.style.fontSize * 0.55;
    });
    return width;
}

constexpr double kLineHeightFactor = 1.4;

}  // namespace

// ---------------------------------------------------------------- geometry --

Point shiftConstrain(Point start, Point current, bool square) {
    const double dx = current.x - start.x;
    const double dy = current.y - start.y;
    if (square) {
        const double side = std::max(std::abs(dx), std::abs(dy));
        return Point{start.x + (dx < 0 ? -side : side),
                     start.y + (dy < 0 ? -side : side)};
    }
    if (dx == 0 && dy == 0) { return current; }
    // Snap to the nearest multiple of 45°, extending by the dominant drag
    // axis so near-horizontal drags keep the mouse's x (projection feel).
    // MSVC's <cmath> hides M_PI behind _USE_MATH_DEFINES; a local constant
    // keeps this portable.
    constexpr double kQuarterPi = 0.78539816339744830962;
    const double snapped = std::round(std::atan2(dy, dx) / kQuarterPi) * kQuarterPi;
    const double s = std::max(std::abs(dx), std::abs(dy));
    return Point{start.x + s * std::cos(snapped),
                 start.y + s * std::sin(snapped)};
}

Rect normalizeRect(Point start, Point current) {
    return Rect{std::min(start.x, current.x), std::min(start.y, current.y),
                std::abs(current.x - start.x), std::abs(current.y - start.y)};
}

void translateAnnotation(Annotation& a, double dx, double dy) {
    std::visit(
        [&](auto& payload) {
            using T = std::decay_t<decltype(payload)>;
            if constexpr (std::is_same_v<T, RectangleAnnotation> ||
                          std::is_same_v<T, EllipseAnnotation> ||
                          std::is_same_v<T, MosaicAnnotation> ||
                          std::is_same_v<T, HighlightAnnotation> ||
                          std::is_same_v<T, PrivacyRedactionAnnotation>) {
                payload.rect.x += dx;
                payload.rect.y += dy;
            } else if constexpr (std::is_same_v<T, LineAnnotation>) {
                payload.startPoint.x += dx; payload.startPoint.y += dy;
                payload.endPoint.x += dx;   payload.endPoint.y += dy;
            } else if constexpr (std::is_same_v<T, ArrowAnnotation>) {
                payload.startPoint.x += dx; payload.startPoint.y += dy;
                payload.endPoint.x += dx;   payload.endPoint.y += dy;
                if (payload.controlPoint) {
                    payload.controlPoint->x += dx;
                    payload.controlPoint->y += dy;
                }
            } else if constexpr (std::is_same_v<T, FreehandAnnotation>) {
                for (Point& p : payload.points) {
                    p.x += dx;
                    p.y += dy;
                }
            } else if constexpr (std::is_same_v<T, TextAnnotation>) {
                payload.position.x += dx;
                payload.position.y += dy;
            } else if constexpr (std::is_same_v<T, NumberLabelAnnotation>) {
                payload.position.x += dx;
                payload.position.y += dy;
                if (payload.leadPoint) {
                    payload.leadPoint->x += dx;
                    payload.leadPoint->y += dy;
                }
            } else if constexpr (std::is_same_v<T, CalloutBubbleAnnotation>) {
                payload.rect.x += dx;
                payload.rect.y += dy;
                payload.targetPoint.x += dx;
                payload.targetPoint.y += dy;
            }
        },
        a.payload);
}

Rect estimatedTextBounds(const TextAnnotation& t) {
    if (t.content.empty()) {
        return Rect{t.position.x, t.position.y, 0, 0};
    }
    const double lineHeight = t.style.fontSize * kLineHeightFactor;
    const double totalWidth = estimatedTextWidth(t);
    double lines = 1.0;
    double width = totalWidth;
    if (t.wrapWidth && *t.wrapWidth > 0 && totalWidth > *t.wrapWidth) {
        width = *t.wrapWidth;
        lines = std::ceil(totalWidth / *t.wrapWidth);
    }
    return Rect{t.position.x, t.position.y, width, lines * lineHeight};
}

const Annotation* hitTest(const std::vector<Annotation>& annotations, Point p) {
    for (auto it = annotations.rbegin(); it != annotations.rend(); ++it) {
        const Annotation& a = *it;
        Rect bounds = std::visit(
            [](const auto& payload) -> Rect {
                using T = std::decay_t<decltype(payload)>;
                if constexpr (std::is_same_v<T, TextAnnotation>) {
                    return estimatedTextBounds(payload);
                } else {
                    return payload.bounds();
                }
            },
            a.payload);
        // Negative-width/height bounds (arrow padding can invert nothing, but
        // keep the guard cheap and symmetric with Geometry::contains).
        if (bounds.width < 0) { bounds.x += bounds.width; bounds.width = -bounds.width; }
        if (bounds.height < 0) { bounds.y += bounds.height; bounds.height = -bounds.height; }
        if (bounds.contains(p)) { return &a; }
    }
    return nullptr;
}

// ---------------------------------------------------------------- document --

void AnnotationDocument::pushUndo() {
    undoStack_.push_back(annotations_);
    if (undoStack_.size() > kAnnotationUndoLimit) {
        undoStack_.erase(undoStack_.begin());  // cap 50: keep the newest
    }
    redoStack_.clear();
}

void AnnotationDocument::add(Annotation a) {
    pushUndo();
    annotations_.push_back(std::move(a));
}

bool AnnotationDocument::remove(const Uuid& id) {
    const auto it = std::find_if(annotations_.begin(), annotations_.end(),
                                 [&id](const Annotation& a) { return a.id() == id; });
    if (it == annotations_.end()) { return false; }
    pushUndo();
    annotations_.erase(it);
    return true;
}

bool AnnotationDocument::move(const Uuid& id, double dx, double dy) {
    const Annotation* found = findById(id);
    if (!found) { return false; }
    pushUndo();
    // findById points into annotations_; indices stay valid across pushUndo.
    const auto it = std::find_if(annotations_.begin(), annotations_.end(),
                                 [&id](const Annotation& a) { return a.id() == id; });
    translateAnnotation(*it, dx, dy);
    return true;
}

void AnnotationDocument::beginBatch() {
    pushUndo();
}

bool AnnotationDocument::translateDirect(const Uuid& id, double dx, double dy) {
    const auto it = std::find_if(annotations_.begin(), annotations_.end(),
                                 [&id](const Annotation& a) { return a.id() == id; });
    if (it == annotations_.end()) { return false; }
    translateAnnotation(*it, dx, dy);
    return true;
}

void AnnotationDocument::setAll(std::vector<Annotation> replacement) {
    pushUndo();
    annotations_ = std::move(replacement);
}

void AnnotationDocument::undo() {
    if (undoStack_.empty()) { return; }
    redoStack_.push_back(std::move(annotations_));
    annotations_ = std::move(undoStack_.back());
    undoStack_.pop_back();
}

void AnnotationDocument::redo() {
    if (redoStack_.empty()) { return; }
    undoStack_.push_back(std::move(annotations_));
    annotations_ = std::move(redoStack_.back());
    redoStack_.pop_back();
}

const Annotation* AnnotationDocument::findById(const Uuid& id) const {
    const auto it = std::find_if(annotations_.begin(), annotations_.end(),
                                 [&id](const Annotation& a) { return a.id() == id; });
    return it == annotations_.end() ? nullptr : &*it;
}

}  // namespace tf::core
