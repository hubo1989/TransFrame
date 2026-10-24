import Foundation
import CoreGraphics
import AppKit

// E2E input synthesis + deterministic pixel assertions for TransFrame.
// Usage:
//   eventsynth drag x1 y1 x2 y2 [cmd|shift]
//   eventsynth click x y [double]
//   eventsynth type "text"
//   eventsynth key escape|return|tab
//   eventsynth shot x y w h out.png
//   eventsynth redbbox in.png          -> "x y w h" of red-ish pixels
//   eventsynth diff a.png b.png        -> changed fraction 0...1

func mouse(_ type: CGEventType, _ x: Double, _ y: Double, flags: CGEventFlags = [], button: CGMouseButton = .left) {
    guard let ev = CGEvent(mouseEventSource: nil, mouseType: type,
                           mouseCursorPosition: CGPoint(x: x, y: y), mouseButton: button) else { return }
    ev.flags = flags
    ev.post(tap: .cghidEventTap)
    usleep(20_000)
}

let args = CommandLine.arguments
guard args.count >= 2 else { fputs("usage: eventsynth <cmd>\n", stderr); exit(2) }

switch args[1] {
case "drag":
    let x1 = Double(args[2])!, y1 = Double(args[3])!, x2 = Double(args[4])!, y2 = Double(args[5])!
    var flags: CGEventFlags = []
    if args.count > 6 {
        if args[6].contains("cmd") { flags.insert(.maskCommand) }
        if args[6].contains("shift") { flags.insert(.maskShift) }
    }
    mouse(.mouseMoved, x1, y1, flags: flags)
    usleep(30_000)
    mouse(.leftMouseDown, x1, y1, flags: flags)
    usleep(30_000)
    let steps = 12
    for i in 1...steps {
        let t = Double(i) / Double(steps)
        mouse(.leftMouseDragged, x1 + (x2 - x1) * t, y1 + (y2 - y1) * t, flags: flags)
    }
    usleep(30_000)
    mouse(.leftMouseUp, x2, y2, flags: flags)

case "click":
    let x = Double(args[2])!, y = Double(args[3])!
    mouse(.mouseMoved, x, y)
    mouse(.leftMouseDown, x, y)
    mouse(.leftMouseUp, x, y)
    if args.count > 4 && args[4] == "double" {
        usleep(60_000)
        mouse(.leftMouseDown, x, y)
        mouse(.leftMouseUp, x, y)
    }

case "type":
    let text = args[2]
    for ch in text {
        let utf = Array(String(ch).utf16)
        for down in [true, false] {
            if let ev = CGEvent(keyboardEventSource: nil, virtualKey: 0, keyDown: down) {
                utf.withUnsafeBufferPointer { buf in
                    ev.keyboardSetUnicodeString(stringLength: buf.count, unicodeString: buf.baseAddress!)
                }
                ev.post(tap: .cghidEventTap)
                usleep(8_000)
            }
        }
    }

case "move":
    // Hover without clicking — used to verify tooltips
    let mx = Double(args[2])!, my = Double(args[3])!
    mouse(.mouseMoved, mx, my)

case "copy":
    // Cmd+C to copy the current selection
    for down in [true, false] {
        if let ev = CGEvent(keyboardEventSource: nil, virtualKey: 8, keyDown: down) {
            ev.flags = .maskCommand
            ev.post(tap: .cghidEventTap)
            usleep(20_000)
        }
    }

case "key":
    let map: [String: CGKeyCode] = ["escape": 53, "return": 36, "tab": 48, "space": 49, "delete": 51]
    guard let code = map[args[2]] else { fputs("unknown key\n", stderr); exit(2) }
    for down in [true, false] {
        if let ev = CGEvent(keyboardEventSource: nil, virtualKey: code, keyDown: down) {
            ev.post(tap: .cghidEventTap)
            usleep(15_000)
        }
    }

case "shot":
    // Delegate to the system screencapture tool (point coordinates, rect crop)
    let x = args[2], y = args[3], w = args[4], h = args[5]
    let out = args[6]
    let p = Process()
    p.executableURL = URL(fileURLWithPath: "/usr/sbin/screencapture")
    p.arguments = ["-x", "-R", "\(x),\(y),\(w),\(h)", out]
    try! p.run()
    p.waitUntilExit()
    exit(p.terminationStatus)

case "redbbox":
    // Find the tight bbox of strongly red pixels (default annotation stroke)
    let url = URL(fileURLWithPath: args[2])
    guard let src = NSImage(contentsOf: url),
          let cg = src.cgImage(forProposedRect: nil, context: nil, hints: nil),
          let data = cg.dataProvider?.data else { exit(1) }
    let bytes = CFDataGetBytePtr(data)!
    let w = cg.width, h = cg.height, bpr = cg.bytesPerRow, bpp = cg.bitsPerPixel / 8
    var minX = w, minY = h, maxX = -1, maxY = -1, count = 0
    for y in 0..<h {
        for x in 0..<w {
            let o = y * bpr + x * bpp
            let r = Int(bytes[o]), g = Int(bytes[o + 1]), b = Int(bytes[o + 2])
            // Stroke reds/oranges: strong red dominance over green and blue
            if r > 140 && r > g + 40 && r > b + 40 {
                minX = min(minX, x); minY = min(minY, y)
                maxX = max(maxX, x); maxY = max(maxY, y)
                count += 1
            }
        }
    }
    if maxX < 0 {
        print("NONE")
    } else {
        print("\(minX) \(minY) \(maxX - minX + 1) \(maxY - minY + 1) \(count)")
    }

case "diff":
    guard let a = NSImage(contentsOf: URL(fileURLWithPath: args[2])),
          let b = NSImage(contentsOf: URL(fileURLWithPath: args[3])),
          let ca = a.cgImage(forProposedRect: nil, context: nil, hints: nil),
          let cb = b.cgImage(forProposedRect: nil, context: nil, hints: nil),
          let da = ca.dataProvider?.data, let db = cb.dataProvider?.data else { exit(1) }
    let ba = CFDataGetBytePtr(da)!, bb = CFDataGetBytePtr(db)!
    let w = min(ca.width, cb.width), h = min(ca.height, cb.height)
    let ra = ca.bytesPerRow, rb = cb.bytesPerRow, pa = ca.bitsPerPixel / 8, pb = cb.bitsPerPixel / 8
    var changed = 0
    for y in 0..<h {
        for x in 0..<w {
            let oa = y * ra + x * pa, ob = y * rb + x * pb
            let dr = abs(Int(ba[oa]) - Int(bb[ob]))
            let dg = abs(Int(ba[oa + 1]) - Int(bb[ob + 1]))
            let dbv = abs(Int(ba[oa + 2]) - Int(bb[ob + 2]))
            if dr + dg + dbv > 40 { changed += 1 }
        }
    }
    let frac = Double(changed) / Double(max(1, w * h))
    print(String(format: "%.5f", frac))

default:
    fputs("unknown command\n", stderr); exit(2)
}
