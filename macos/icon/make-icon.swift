import AppKit
let cs = CGColorSpace(name: CGColorSpace.sRGB)!
let ctx = CGContext(data: nil, width: 1024, height: 1024, bitsPerComponent: 8, bytesPerRow: 0, space: cs, bitmapInfo: CGImageAlphaInfo.premultipliedLast.rawValue)!
func c(_ r: CGFloat,_ g: CGFloat,_ b: CGFloat,_ a: CGFloat = 1) -> CGColor { CGColor(srgbRed: r/255, green: g/255, blue: b/255, alpha: a) }
let shape = CGPath(roundedRect: CGRect(x: 100, y: 100, width: 824, height: 824), cornerWidth: 185, cornerHeight: 185, transform: nil)
ctx.saveGState(); ctx.setShadow(offset: CGSize(width: 0, height: -12), blur: 28, color: c(0,0,0,0.45)); ctx.addPath(shape); ctx.setFillColor(c(16,17,19)); ctx.fillPath(); ctx.restoreGState()
ctx.saveGState(); ctx.addPath(shape); ctx.clip()
let bg = CGGradient(colorsSpace: cs, colors: [c(40,42,46), c(20,21,24), c(8,8,10)] as CFArray, locations: [0,0.6,1])!
ctx.drawRadialGradient(bg, startCenter: CGPoint(x: 512, y: 512), startRadius: 0, endCenter: CGPoint(x: 512, y: 512), endRadius: 600, options: [])
// radar: range rings, cross lines, a sweep wedge
let o = CGPoint(x: 512, y: 512)
ctx.setStrokeColor(c(120,200,140,0.10)); ctx.setLineWidth(3)
for r in stride(from: 110.0, through: 440.0, by: 110.0) { ctx.addArc(center: o, radius: r, startAngle: 0, endAngle: .pi*2, clockwise: false); ctx.strokePath() }
ctx.move(to: CGPoint(x: 100, y: 512)); ctx.addLine(to: CGPoint(x: 924, y: 512)); ctx.move(to: CGPoint(x: 512, y: 100)); ctx.addLine(to: CGPoint(x: 512, y: 924)); ctx.strokePath()
let steps = 60
for i in 0..<steps {
    let a0 = CGFloat.pi*0.25 + CGFloat(i)*CGFloat.pi*0.5/CGFloat(steps), a1 = a0 + CGFloat.pi*0.5/CGFloat(steps) + 0.002
    let alpha = 0.16 * CGFloat(i)/CGFloat(steps)
    let p = CGMutablePath(); p.move(to: o); p.addArc(center: o, radius: 460, startAngle: a0, endAngle: a1, clockwise: false); p.closeSubpath()
    ctx.addPath(p); ctx.setFillColor(c(120,220,150,alpha)); ctx.fillPath()
}
ctx.setStrokeColor(c(150,240,170,0.35)); ctx.setLineWidth(4)
ctx.move(to: o); ctx.addLine(to: CGPoint(x: 512 + 460*cos(.pi*0.75), y: 512 + 460*sin(.pi*0.75))); ctx.strokePath()
ctx.restoreGState()

// flat red "2" (original outline)
let pts: [(CGFloat, CGFloat)] = [(300,640),(300,700),(360,770),(650,770),(720,700),(720,560),(470,360),(720,360),(720,250),(300,250),(300,350),(590,585),(590,655),(565,680),(445,680),(420,655),(420,640)]
let g = CGMutablePath(); g.addLines(between: pts.map { CGPoint(x: $0.0, y: $0.1) }); g.closeSubpath()
ctx.saveGState(); ctx.setShadow(offset: CGSize(width: 0, height: -10), blur: 24, color: c(0,0,0,0.6)); ctx.addPath(g); ctx.setFillColor(c(160,18,18)); ctx.fillPath(); ctx.restoreGState()
// small radar blip
ctx.setFillColor(c(170,255,190,0.9)); ctx.fillEllipse(in: CGRect(x: 760, y: 700, width: 22, height: 22))
let rep = NSBitmapImageRep(cgImage: ctx.makeImage()!)
try! rep.representation(using: .png, properties: [:])!.write(to: URL(fileURLWithPath: "OpenBF2-1024.png"))
