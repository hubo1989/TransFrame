// tests/AnnotationDocumentTests.cpp — FR-08/09/11 editing logic: Shift
// constraint, drag normalization, translate, estimated text bounds, hit
// testing, and the 50-step undo/redo document.
#include "AnnotationDocument.h"

#include "catch_amalgamated.hpp"

using namespace tf::core;
using Catch::Approx;

namespace {

RectangleAnnotation makeRect(double x, double y, double w, double h) {
    RectangleAnnotation r;
    r.id = Uuid::randomUuid();
    r.rect = Rect{x, y, w, h};
    r.style = StrokeStyle{CodableColor::redColor(), 2.0};
    return r;
}

ArrowAnnotation makeArrow(double x1, double y1, double x2, double y2) {
    ArrowAnnotation a;
    a.id = Uuid::randomUuid();
    a.startPoint = Point{x1, y1};
    a.endPoint = Point{x2, y2};
    a.style = StrokeStyle{CodableColor::blueColor(), 2.0};
    return a;
}

TextAnnotation makeText(const std::string& content, double x, double y) {
    TextAnnotation t;
    t.id = Uuid::randomUuid();
    t.position = Point{x, y};
    t.content = content;
    t.style = TextStyle{};
    return t;
}

}  // namespace

TEST_CASE("shiftConstrain forces a square for the rectangle tool", "[annotation]") {
    const Point start{10, 10};
    const Point end = shiftConstrain(start, Point{210, 110}, true);
    REQUIRE(end.x - start.x == Approx(200.0));
    REQUIRE(end.y - start.y == Approx(200.0));
    // Opposite direction: a drag toward the lower-left constrains to the
    // lower-left square quadrant (current is an absolute point).
    const Point neg = shiftConstrain(start, Point{start.x - 90, start.y + 30}, true);
    REQUIRE(neg.x - start.x == Approx(-90.0));
    REQUIRE(neg.y - start.y == Approx(90.0));
}

TEST_CASE("shiftConstrain snaps line drags to 45 degree steps", "[annotation]") {
    const Point start{0, 0};
    // Nearly-horizontal drag snaps fully horizontal at the mouse's x.
    Point end = shiftConstrain(start, Point{100, 8}, false);
    REQUIRE(end.x == Approx(100.0).margin(1e-6));
    REQUIRE(end.y == Approx(0.0).margin(1e-9));
    // 31° drag snaps to the 45° diagonal.
    end = shiftConstrain(start, Point{100, 60}, false);
    const double diag = 100.0 / std::sqrt(2.0);
    REQUIRE(end.x == Approx(diag).margin(1e-6));
    REQUIRE(end.y == Approx(diag).margin(1e-6));
    // Vertical drag stays vertical.
    end = shiftConstrain(start, Point{3, 100}, false);
    REQUIRE(end.x == Approx(0.0).margin(1e-9));
    REQUIRE(end.y == Approx(100.0).margin(1e-6));
}

TEST_CASE("normalizeRect covers both drag corners with positive size", "[annotation]") {
    const Rect r = normalizeRect(Point{100, 100}, Point{40, 180});
    REQUIRE(r.x == 40);
    REQUIRE(r.y == 100);
    REQUIRE(r.width == 60);
    REQUIRE(r.height == 80);
}

TEST_CASE("translateAnnotation moves every movable point of a payload", "[annotation]") {
    SECTION("rectangle") {
        auto a = Annotation::rectangle(makeRect(0, 0, 50, 50));
        translateAnnotation(a, 10, -5);
        const auto& r = std::get<RectangleAnnotation>(a.payload);
        REQUIRE(r.rect.x == 10);
        REQUIRE(r.rect.y == -5);
    }
    SECTION("arrow with control point") {
        ArrowAnnotation ar = makeArrow(0, 0, 100, 0);
        ar.controlPoint = Point{50, 40};
        auto a = Annotation::arrow(ar);
        translateAnnotation(a, 5, 5);
        const auto& moved = std::get<ArrowAnnotation>(a.payload);
        REQUIRE(moved.startPoint.x == 5);
        REQUIRE(moved.endPoint.x == 105);
        REQUIRE(moved.controlPoint->x == 55);
        REQUIRE(moved.controlPoint->y == 45);
    }
    SECTION("freehand polyline") {
        FreehandAnnotation f;
        f.id = Uuid::randomUuid();
        f.points = {Point{0, 0}, Point{10, 10}, Point{20, 0}};
        f.style = StrokeStyle{};
        auto a = Annotation::freehand(f);
        translateAnnotation(a, -1, 2);
        const auto& moved = std::get<FreehandAnnotation>(a.payload);
        REQUIRE(moved.points[0].x == -1);
        REQUIRE(moved.points[2].y == 2);
    }
    SECTION("text anchor") {
        auto a = Annotation::text(makeText("hi", 30, 40));
        translateAnnotation(a, 7, 9);
        const auto& t = std::get<TextAnnotation>(a.payload);
        REQUIRE(t.position.x == 37);
        REQUIRE(t.position.y == 49);
    }
}

TEST_CASE("estimatedTextBounds covers latin and cjk content", "[annotation]") {
    const auto latin = makeText("hello", 10, 10);  // 5 * 14 * 0.55 = 38.5
    Rect b = estimatedTextBounds(latin);
    REQUIRE(b.width == Approx(5 * 14.0 * 0.55));
    REQUIRE(b.height == Approx(14.0 * 1.4));

    const auto cjk = makeText("中文文本", 0, 0);  // 4 code points at full width
    b = estimatedTextBounds(cjk);
    REQUIRE(b.width == Approx(4 * 14.0));
    REQUIRE(b.height == Approx(14.0 * 1.4));

    auto wrapped = makeText("aaaaaaaaaa", 0, 0);
    wrapped.wrapWidth = 14.0 * 0.55 * 4;  // 4 latin chars per line -> 3 lines
    b = estimatedTextBounds(wrapped);
    REQUIRE(b.width == Approx(14.0 * 0.55 * 4));
    REQUIRE(b.height == Approx(3 * 14.0 * 1.4));
}

TEST_CASE("hitTest picks the topmost annotation and estimates text bounds", "[annotation]") {
    std::vector<Annotation> list;
    list.push_back(Annotation::rectangle(makeRect(0, 0, 100, 100)));
    list.push_back(Annotation::rectangle(makeRect(50, 50, 100, 100)));

    // Inside the first rect only.
    const Annotation* hit = hitTest(list, Point{20, 20});
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id() == list[0].id());

    // Overlap area -> topmost (last drawn) wins.
    hit = hitTest(list, Point{60, 60});
    REQUIRE(hit != nullptr);
    REQUIRE(hit->id() == list[1].id());

    // Nothing here.
    REQUIRE(hitTest(list, Point{200, 200}) == nullptr);

    // Text hits through its estimated bounds even though Annotation::bounds()
    // collapses to the anchor point.
    list.push_back(Annotation::text(makeText("宽字符标注", 10, 10)));
    hit = hitTest(list, Point{10, 10});
    REQUIRE(hit != nullptr);
    REQUIRE(std::holds_alternative<TextAnnotation>(hit->payload));
}

TEST_CASE("AnnotationDocument undo and redo round-trips mutations", "[annotation]") {
    AnnotationDocument doc;
    REQUIRE_FALSE(doc.canUndo());
    REQUIRE_FALSE(doc.canRedo());

    const auto first = makeRect(0, 0, 50, 50);
    const Uuid firstId = first.id;
    doc.add(Annotation::rectangle(first));
    const auto second = makeRect(200, 200, 50, 50);
    const Uuid secondId = second.id;
    doc.add(Annotation::rectangle(second));
    REQUIRE(doc.annotations().size() == 2);

    doc.undo();
    REQUIRE(doc.annotations().size() == 1);
    REQUIRE(doc.canRedo());
    doc.undo();
    REQUIRE(doc.annotations().empty());

    doc.redo();
    doc.redo();
    REQUIRE(doc.annotations().size() == 2);
    REQUIRE(doc.annotations()[1].id() == secondId);

    // Both redo entries are consumed; a fresh mutation then finds nothing to clear.
    REQUIRE_FALSE(doc.canRedo());
    doc.move(firstId, 5, 5);
    REQUIRE_FALSE(doc.canRedo());
    const auto* moved = doc.findById(firstId);
    REQUIRE(moved != nullptr);
    REQUIRE(std::get<RectangleAnnotation>(moved->payload).rect.x == 5);

    doc.remove(secondId);
    REQUIRE(doc.annotations().size() == 1);
    doc.undo();
    REQUIRE(doc.annotations().size() == 2);
    // Undo past the first add: stack depth was add,add,move,remove = 4.
    doc.undo();  // back to "after second add"? No — after move.
    doc.undo();  // remove undone already; undo the move
    REQUIRE(std::get<RectangleAnnotation>(doc.findById(firstId)->payload).rect.x == 0);
}

TEST_CASE("AnnotationDocument caps the undo stack at 50 steps", "[annotation]") {
    AnnotationDocument doc;
    for (int i = 0; i < 60; ++i) {
        doc.add(Annotation::rectangle(makeRect(i, 0, 50, 50)));
    }
    REQUIRE(doc.annotations().size() == 60);
    int undos = 0;
    while (doc.canUndo()) {
        doc.undo();
        ++undos;
    }
    REQUIRE(undos == static_cast<int>(kAnnotationUndoLimit));
    REQUIRE(doc.annotations().size() == 10);
}

TEST_CASE("AnnotationDocument ignores unknown ids without touching history", "[annotation]") {
    AnnotationDocument doc;
    doc.add(Annotation::rectangle(makeRect(0, 0, 50, 50)));
    const Uuid missing = Uuid::randomUuid();
    REQUIRE_FALSE(doc.remove(missing));
    REQUIRE_FALSE(doc.move(missing, 1, 1));
    // Exactly one history entry (the add): the failed remove/move pushed none.
    doc.undo();
    REQUIRE(doc.annotations().empty());
    REQUIRE_FALSE(doc.canUndo());
}

TEST_CASE("AnnotationDocument setAll is one history step", "[annotation]") {
    AnnotationDocument doc;
    doc.add(Annotation::rectangle(makeRect(0, 0, 50, 50)));
    doc.setAll({});  // crop-clear semantics (FR-11)
    REQUIRE(doc.annotations().empty());
    doc.undo();
    REQUIRE(doc.annotations().size() == 1);
}

TEST_CASE("AnnotationDocument batch move keeps a single history entry", "[annotation]") {
    AnnotationDocument doc;
    doc.add(Annotation::rectangle(makeRect(0, 0, 50, 50)));
    const Uuid id = doc.annotations().back().id();

    // Drag protocol: one snapshot at drag start, silent moves per frame.
    doc.beginBatch();
    REQUIRE(doc.translateDirect(id, 3, 0));
    REQUIRE(doc.translateDirect(id, 4, 0));
    REQUIRE(std::get<RectangleAnnotation>(doc.findById(id)->payload).rect.x == 7);
    doc.undo();  // back to pre-drag in one step
    REQUIRE(std::get<RectangleAnnotation>(doc.findById(id)->payload).rect.x == 0);
    doc.redo();  // drag redone as one step
    REQUIRE(std::get<RectangleAnnotation>(doc.findById(id)->payload).rect.x == 7);
}
