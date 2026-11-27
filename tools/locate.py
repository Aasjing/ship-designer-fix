"""在 4.6 exe 里定位舰船设计器“装配/填充空槽”相关函数。

锚点全是唯一字符串（exe 里各只有一处引用），所以换小版本也能复现：
  1. "COMPONENT_CANT_BUILD_MISSING_TECH" -> NShipDesignUtil::CanBuildComponent
  2. "invalid component template for ship design" -> CCountry::AddRequiredComponentsToShipDesign
     （它内部用的是带科技校验的 CComponentSet::FindBestRequiredComponent）
用法: python tools/locate.py [--callers RVA] [--disasm RVA] [--calls RVA]
"""
import importlib.util
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, "..", ".."))
FUI = os.path.join(ROOT, "gamesetup_ext", "tools", "find_ui_inject.py")

spec = importlib.util.spec_from_file_location("fui", FUI)
fui = importlib.util.module_from_spec(spec)
spec.loader.exec_module(fui)

ANCHORS = [
    (b"COMPONENT_CANT_BUILD_MISSING_TECH", "CanBuildComponent"),
    (b"invalid component template for ship design", "AddRequiredComponentsToShipDesign"),
    (b"apply_on_empty", "component selector (apply_on_empty)"),
]


def main():
    data, base, ts, sections, pdata_rva, pdata_size = fui.load(fui.DEFAULT_EXE)
    pdata = fui.Pdata(data, sections, pdata_rva, pdata_size)
    print("exe build timestamp: 0x%X" % ts)

    found = {}
    for needle, label in ANCHORS:
        va = fui.find_string_va(data, sections, base, needle)
        if va is None:
            print("%-42s 字符串没找到" % label)
            continue
        hits = fui.scan_rip_refs(data, sections, base, {va})
        funcs = sorted({pdata.function(r - base)[0] for r in hits.get(va, []) if pdata.function(r - base)[0]})
        print("%-42s str=0x%X 引用函数 %s" % (label, va, [hex(f) for f in funcs]))
        for f in funcs:
            found.setdefault(label, []).append(f)

    # AddRequiredComponentsToShipDesign 里会 call FindBestRequiredComponent（带科技校验的选件）
    for f in found.get("AddRequiredComponentsToShipDesign", []):
        print("\n%s (0x%X) 的 call 目标：" % ("AddRequiredComponentsToShipDesign", f))
        seen = {}
        for insn in fui.disasm_function(data, sections, base, f, pdata.function(f)[1]):
            if insn["imm"]:
                seen.setdefault(insn["imm"] - base, 0)
                seen[insn["imm"] - base] += 1
        for rva, n in sorted(seen.items()):
            fn = pdata.function(rva)[0]
            print("   -> rva 0x%-9X func 0x%-9X x%d" % (rva, fn if fn else 0, n))

    if len(sys.argv) > 2 and sys.argv[1] == "--disasm":
        rva = int(sys.argv[2], 16)
        start, end = pdata.function(rva)
        print("\n=== disasm 0x%X func 0x%X..0x%X" % (rva, start, end))
        for insn in fui.disasm_function(data, sections, base, start, end):
            rip = (" -> %s" % hex(insn["rip"])) if insn["rip"] else ""
            imm = (" -> rva %s" % hex(insn["imm"] - base)) if insn["imm"] else ""
            print("   %s %-8s %s%s%s" % (hex(insn["rva"]), insn["mnem"], insn["op"], rip, imm))
    elif len(sys.argv) > 2 and sys.argv[1] == "--callers":
        rva = int(sys.argv[2], 16)
        for site in fui.scan_call_sites(data, sections, base, rva):
            fn = pdata.function(site)[0]
            print("call site 0x%X in func 0x%X" % (site, fn if fn else 0))


main()
