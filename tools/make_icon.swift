// アプリアイコン（1024×1024 PNG）を CoreGraphics で描画するスクリプト。
// 使い方:  swift tools/make_icon.swift assets/icons/icon_1024.png
// 生成された PNG を CMake の ICON_BIG に渡すと、juceaide が macOS 用 .icns を作る。

import CoreGraphics
import Foundation
import ImageIO
import UniformTypeIdentifiers

let arguments = CommandLine.arguments

guard arguments.count >= 2 else {
    fputs("usage: swift make_icon.swift <output.png>\n", stderr)
    exit(2)
}

let size = 1024

guard let colorSpace = CGColorSpace(name: CGColorSpace.sRGB),
      let ctx = CGContext(
          data: nil, width: size, height: size, bitsPerComponent: 8, bytesPerRow: 0,
          space: colorSpace, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)
else {
    fputs("failed to create CGContext\n", stderr)
    exit(1)
}

ctx.clear(CGRect(x: 0, y: 0, width: size, height: size))

// macOS のアイコン規格に合わせ、824×824 のスクエークルを中央に置く（余白100px）
let inset: CGFloat = 100
let rect = CGRect(x: inset, y: inset, width: CGFloat(size) - 2 * inset, height: CGFloat(size) - 2 * inset)
let radius: CGFloat = 185

ctx.saveGState()
ctx.addPath(CGPath(roundedRect: rect, cornerWidth: radius, cornerHeight: radius, transform: nil))
ctx.clip()

// 背景: 対角グラデーションのダークチャコール
let backgroundColors = [
    CGColor(red: 0.16, green: 0.17, blue: 0.20, alpha: 1),
    CGColor(red: 0.06, green: 0.065, blue: 0.08, alpha: 1),
] as CFArray

if let gradient = CGGradient(colorsSpace: colorSpace, colors: backgroundColors, locations: [0, 1]) {
    ctx.drawLinearGradient(
        gradient,
        start: CGPoint(x: 0, y: CGFloat(size)),
        end: CGPoint(x: CGFloat(size), y: 0),
        options: [])
}

// 薄いグリッド線（測定ソフトの画面を想起させる）
ctx.setFillColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.05))
for y in [CGFloat(384), CGFloat(512), CGFloat(640)] {
    ctx.fill(CGRect(x: inset, y: y, width: rect.width, height: 2))
}

// 伝達関数風のカーブ（山と谷のある滑らかな応答曲線）
let points: [CGPoint] = [
    CGPoint(x: 190, y: 480), CGPoint(x: 300, y: 415), CGPoint(x: 410, y: 565),
    CGPoint(x: 515, y: 355), CGPoint(x: 640, y: 590), CGPoint(x: 745, y: 455),
    CGPoint(x: 835, y: 515),
]

let curve = CGMutablePath()
curve.move(to: points[0])

for i in 0..<(points.count - 1) {
    let p0 = points[max(i - 1, 0)]
    let p1 = points[i]
    let p2 = points[i + 1]
    let p3 = points[min(i + 2, points.count - 1)]

    curve.addCurve(
        to: p2,
        control1: CGPoint(x: p1.x + (p2.x - p0.x) / 6, y: p1.y + (p2.y - p0.y) / 6),
        control2: CGPoint(x: p2.x - (p3.x - p1.x) / 6, y: p2.y - (p3.y - p1.y) / 6))
}

ctx.setLineCap(.round)
ctx.setLineJoin(.round)

// グロー（広く弱い光 → 細く強い芯）
ctx.setShadow(offset: .zero, blur: 60, color: CGColor(red: 0.25, green: 0.85, blue: 0.95, alpha: 0.55))
ctx.setStrokeColor(CGColor(red: 0.35, green: 0.85, blue: 0.93, alpha: 0.45))
ctx.setLineWidth(34)
ctx.addPath(curve)
ctx.strokePath()

ctx.setShadow(offset: .zero, blur: 16, color: CGColor(red: 0.35, green: 0.9, blue: 1.0, alpha: 0.9))
ctx.setStrokeColor(CGColor(red: 0.82, green: 0.98, blue: 1.0, alpha: 1))
ctx.setLineWidth(13)
ctx.addPath(curve)
ctx.strokePath()
ctx.setShadow(offset: .zero, blur: 0, color: nil)

// 内側の細い縁取り
ctx.setStrokeColor(CGColor(red: 1, green: 1, blue: 1, alpha: 0.07))
ctx.setLineWidth(2)
ctx.addPath(CGPath(
    roundedRect: rect.insetBy(dx: 1.5, dy: 1.5),
    cornerWidth: radius - 1.5, cornerHeight: radius - 1.5, transform: nil))
ctx.strokePath()

ctx.restoreGState()

guard let image = ctx.makeImage(),
      let destination = CGImageDestinationCreateWithURL(
          URL(fileURLWithPath: arguments[1]) as CFURL,
          UTType.png.identifier as CFString, 1, nil)
else {
    fputs("failed to write \(arguments[1])\n", stderr)
    exit(1)
}

CGImageDestinationAddImage(destination, image, nil)
CGImageDestinationFinalize(destination)
print("icon written: \(arguments[1])")
