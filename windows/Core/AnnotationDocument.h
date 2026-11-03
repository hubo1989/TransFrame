#pragma once
// AnnotationDocument.h — annotation editing document (PRD FR-08/09/11).
// Ordered annotation list + 50-step undo/redo, topmost-first hit testing,
// drag-geometry helpers (Shift constraint, rect normalization, translate).
//
// Pure Core logic: no UI, no Win32 — Catch2-testable on any OS. Drawing and
// mouse interaction live in NativeShell (PreviewWindow); this layer owns the
// data and decisions so the editor behavior stays identical across hosts.
//
// Coordinates are image pixel coordinates (top-left origin), matching
// Annotation.h.

#include "Annotation.h"
#include "Geometry.h"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace tf::core {

/// Undo history cap (PRD FR-11: 撤销栈 50 步). The newest entries are kept;
/// pushing beyond the cap drops the oldest snapshot.
inline constexpr std::size_t kAnnotationUndoLimit = 50;

/// Editor tools for the minimal annotation set (PRD FR-08 minimal:
/// 矩形/文字/箭头; full 11-type rendering is driven by the payload variant).
enum class AnnotationTool : std::int32_t {
    None,
    Rectangle,
    Text,
    Arrow,
};

/// Shift-constrain a drag (PRD FR-08: Shift+拖拽 = 正方形 / 水平垂直线 / 45° 箭头).
/// `square` (rectangle tool): force |w| == |h| around the start corner.
/// Otherwise (line/arrow tool): snap the start->current direction to the
/// nearest multiple of 45°, keeping the raw drag length.
Point shiftConstrain(Point start, Point current, bool square);

/// Drag normalization: an any-direction drag becomes a positive, origin-sorted
/// rect that covers both points. Degenerate drags (< 1px) return a rect that
/// fails the >= 5px validity checks of the rect payloads.
Rect normalizeRect(Point start, Point current);

/// Translate every movable point of `a` by (dx, dy). Handles all 11 payload
/// types (rect-based, point-based, polyline). Never mutates styles.
void translateAnnotation(Annotation& a, double dx, double dy);

/// Estimated text bounds for hit testing and selection drawing. Core has no
/// font metrics: width per code point ≈ fontSize (CJK) / fontSize * 0.55
/// (Latin), line height ≈ fontSize * 1.4, wrapped at `wrapWidth` when set.
/// The UI layer may override with measured metrics; this estimate only has to
/// rank hit tests sensibly.
Rect estimatedTextBounds(const TextAnnotation& t);

/// Topmost-first hit test: the last annotation whose (estimated) bounds
/// contain `p`. Text annotations hit-test via estimatedTextBounds.
/// Returns nullptr when nothing matches.
const Annotation* hitTest(const std::vector<Annotation>& annotations, Point p);

/// Annotation stack (index 0 = bottom, drawn first; back = topmost) with
/// snapshot-based undo/redo. Every mutation pushes a snapshot of the current
/// list before changing it; a new mutation clears the redo stack.
class AnnotationDocument {
public:
    /// Append on top. Invalid payloads (e.g. a 2px drag rect) are still
    /// stored — validity is the caller's/editor's gate (mirrors macOS,
    /// which validates on drag-completion, not on model add).
    void add(Annotation a);

    /// Remove by id. Returns false (without touching history) when absent.
    bool remove(const Uuid& id);

    /// Translate by (dx, dy). Returns false (no history entry) when absent.
    bool move(const Uuid& id, double dx, double dy);

    /// Interactive move protocol: call beginBatch() once at drag start (it
    /// pushes exactly one history snapshot), then translateDirect() per mouse
    /// frame without touching the undo/redo stacks.
    void beginBatch();
    bool translateDirect(const Uuid& id, double dx, double dy);

    /// Replace the whole list in one history step (crop-clears-annotations,
    /// PRD FR-11, or future batch import).
    void setAll(std::vector<Annotation> replacement);

    void undo();   // no-op when !canUndo()
    void redo();   // no-op when !canRedo()
    bool canUndo() const { return !undoStack_.empty(); }
    bool canRedo() const { return !redoStack_.empty(); }

    const std::vector<Annotation>& annotations() const { return annotations_; }

    const Annotation* hitTest(Point p) const { return tf::core::hitTest(annotations_, p); }
    const Annotation* findById(const Uuid& id) const;

private:
    void pushUndo();

    std::vector<Annotation> annotations_;
    std::vector<std::vector<Annotation>> undoStack_;
    std::vector<std::vector<Annotation>> redoStack_;
};

}  // namespace tf::core
