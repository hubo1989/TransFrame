import XCTest
@testable import TransFrame

final class CoordinateMapperTests: XCTestCase {
    func testCocoaToQuartzAndBackWithSingleScreen() {
        let primaryHeight: CGFloat = 1080
        // Window located at (100, 200) with size (300, 400) in Cocoa (bottom-left origin)
        let cocoa = CocoaGlobalRect(CGRect(x: 100, y: 200, width: 300, height: 400))
        let quartz = CoordinateMapper.cocoaToQuartz(cocoa, primaryHeight: primaryHeight)

        // y in quartz: 1080 - (200 + 400) = 480
        XCTAssertEqual(quartz.rawValue.origin.x, 100)
        XCTAssertEqual(quartz.rawValue.origin.y, 480)
        XCTAssertEqual(quartz.rawValue.width, 300)
        XCTAssertEqual(quartz.rawValue.height, 400)

        // Reverse back
        let back = CoordinateMapper.quartzToCocoa(quartz, primaryHeight: primaryHeight)
        XCTAssertEqual(back, cocoa)
    }

    func testNegativeCoordinatesForLeftSecondaryMonitor() {
        let primaryHeight: CGFloat = 1080
        // Secondary screen on the left of primary: x = -1920, y = 0, size = 1920x1080 in Cocoa
        let secondaryCocoa = CocoaGlobalRect(CGRect(x: -1920, y: 0, width: 1920, height: 1080))
        let quartz = CoordinateMapper.cocoaToQuartz(secondaryCocoa, primaryHeight: primaryHeight)

        XCTAssertEqual(quartz.rawValue.origin.x, -1920)
        XCTAssertEqual(quartz.rawValue.origin.y, 0)
        XCTAssertEqual(quartz.rawValue.width, 1920)
        XCTAssertEqual(quartz.rawValue.height, 1080)

        let back = CoordinateMapper.quartzToCocoa(quartz, primaryHeight: primaryHeight)
        XCTAssertEqual(back, secondaryCocoa)
    }

    func testQuartzToDisplayLocalAndBack() {
        let displayQuartz = QuartzGlobalRect(CGRect(x: 1920, y: 0, width: 2560, height: 1440))
        let windowQuartz = QuartzGlobalRect(CGRect(x: 2000, y: 100, width: 800, height: 600))

        let local = CoordinateMapper.quartzToDisplayLocal(windowQuartz, displayQuartzBounds: displayQuartz)
        XCTAssertEqual(local.rawValue, CGRect(x: 80, y: 100, width: 800, height: 600))

        let back = CoordinateMapper.displayLocalToQuartz(local, displayQuartzBounds: displayQuartz)
        XCTAssertEqual(back, windowQuartz)
    }

    func testDisplayLocalToPixelFloorCeilRounding() {
        let local = DisplayLocalRect(CGRect(x: 10.2, y: 20.7, width: 100.3, height: 50.1))
        let scale: CGFloat = 2.0
        let imageSize = CGSize(width: 500, height: 500)

        let pixel = CoordinateMapper.displayLocalToPixel(local, scaleFactor: scale, imageSize: imageSize)

        // minX: floor(10.2 * 2 = 20.4) = 20
        // minY: floor(20.7 * 2 = 41.4) = 41
        // maxX: ceil((10.2 + 100.3) * 2 = 110.5 * 2 = 221.0) = 221
        // maxY: ceil((20.7 + 50.1) * 2 = 70.8 * 2 = 141.6) = 142
        XCTAssertEqual(pixel.rawValue.minX, 20)
        XCTAssertEqual(pixel.rawValue.minY, 41)
        XCTAssertEqual(pixel.rawValue.maxX, 221)
        XCTAssertEqual(pixel.rawValue.maxY, 142)
    }

    func testDisplayLocalToPixelBoundaryClamping() {
        let local = DisplayLocalRect(CGRect(x: -10, y: -20, width: 1000, height: 1000))
        let scale: CGFloat = 1.0
        let imageSize = CGSize(width: 500, height: 400)

        let pixel = CoordinateMapper.displayLocalToPixel(local, scaleFactor: scale, imageSize: imageSize)

        XCTAssertEqual(pixel.rawValue.minX, 0)
        XCTAssertEqual(pixel.rawValue.minY, 0)
        XCTAssertEqual(pixel.rawValue.maxX, 500)
        XCTAssertEqual(pixel.rawValue.maxY, 400)
    }
}
