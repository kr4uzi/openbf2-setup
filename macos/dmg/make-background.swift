// Draws the background of the disk image: a plain light panel with a thin arrow from where the app sits to
// where the Applications folder sits. 560x360 points, rendered at 2x. Run: swift make-background.swift
import AppKit
let width = 1120, height = 720
let cs = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: width, height: height, bitsPerComponent: 8, bytesPerRow: 0, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
func c(_ r: CGFloat, _ g: CGFloat, _ b: CGFloat, _ a: CGFloat = 1) -> CGColor { CGColor(srgbRed: r/255, green: g/255, blue: b/255, alpha: a) }
ctx.setFillColor(c(239, 236, 230)); ctx.fill(CGRect(x: 0, y: 0, width: width, height: height))
// the arrow, between the two icons (centres at x 140 and 420 points, y 170 points from the top)
let y = CGFloat(height) - 170 * 2
ctx.setStrokeColor(c(70, 68, 63)); ctx.setLineWidth(9); ctx.setLineCap(.round); ctx.setLineJoin(.round)
ctx.move(to: CGPoint(x: 500, y: y)); ctx.addLine(to: CGPoint(x: 620, y: y)); ctx.strokePath()
ctx.move(to: CGPoint(x: 572, y: y + 48)); ctx.addLine(to: CGPoint(x: 620, y: y)); ctx.addLine(to: CGPoint(x: 572, y: y - 48)); ctx.strokePath()
// written as 560x360 points, so the Finder shows it at that size on any display
let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
rep.size = NSSize(width: 560, height: 360)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "background.png"))
