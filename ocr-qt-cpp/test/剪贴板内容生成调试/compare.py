import re

SCRIPT_DIR = r"C:\Users\zhuyue\Desktop\RapidOcrEmbed\剪贴板复制"

with open(f"{SCRIPT_DIR}\\generated_clipboard.txt", "r", encoding="utf-8") as f:
    gen = f.read()
with open(f"{SCRIPT_DIR}\\html的正确版本.txt", "r", encoding="utf-8") as f:
    ref = f.read()

gen_frag = re.search(r"<!--StartFragment-->(.*?)<!--EndFragment-->", gen, re.S)
ref_frag = re.search(r"<!--StartFragment-->(.*?)<!--EndFragment-->", ref, re.S)

if gen_frag and ref_frag:
    gen_t = gen_frag.group(1)
    ref_t = ref_frag.group(1)
    print("=== Structure comparison ===")
    for tag in ["table", "tbody", "/tbody", "colgroup", "/colgroup", "col ", "tr ", "td", "/td"]:
        g = gen_t.count(f"<{tag}") if not tag.startswith("/") else gen_t.count(f"<{tag}")
        r = ref_t.count(f"<{tag}") if not tag.startswith("/") else ref_t.count(f"<{tag}")
        ok = "OK" if g == r else "MISMATCH"
        print(f"  <{tag}>: gen={g}, ref={r} [{ok}]")

    print(f"\n  colspan: gen={gen_t.count('colspan')}, ref={ref_t.count('colspan')}")
    print(f"  border: 1px: gen={gen_t.count('border: 1px solid black')}, ref={ref_t.count('border: 1px solid black')}")

    # Check if fragments look structurally the same
    gen_cols = re.findall(r'<col[^>]*>', gen_t)
    ref_cols = re.findall(r'<col[^>]*>', ref_t)
    print(f"\n  col count: gen={len(gen_cols)}, ref={len(ref_cols)}")
    
    gen_trs = re.findall(r'<tr[^>]*>', gen_t)
    ref_trs = re.findall(r'<tr[^>]*>', ref_t)
    print(f"  tr count: gen={len(gen_trs)}, ref={len(ref_trs)}")

    gen_tds = re.findall(r'<td[^>]*>', gen_t)
    ref_tds = re.findall(r'<td[^>]*>', ref_t)
    print(f"  td count: gen={len(gen_tds)}, ref={len(ref_tds)}")
else:
    print("ERROR: Could not extract fragments")
