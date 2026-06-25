"""
验证批量表格：从 程序生成的html.html 生成 Excel 兼容的 CF_HTML 剪贴板数据
"""
import os
import re
import ctypes

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
SOURCE_HTML = os.path.join(SCRIPT_DIR, "程序生成的html.html")
OUTPUT = os.path.join(SCRIPT_DIR, "generated_clipboard.txt")

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
GMEM_MOVEABLE = 0x0002
CF_UNICODETEXT = 13


def count_cols(table_html):
    m = re.search(r"<tr[^>]*>(.*?)</tr>", table_html, re.I | re.S)
    if not m:
        return 8
    cols = 0
    for td in re.finditer(r"<td[^>]*>", m.group(1), re.I):
        cs = re.search(r"colspan\s*=\s*['\"]?(\d+)['\"]?", td.group(0), re.I)
        cols += int(cs.group(1)) if cs else 1
    return cols if cols > 0 else 8


def process_row(row_content, rh):
    td_open = re.compile(r"<td([^>]*)>", re.I)
    td_close = re.compile(r"</td>", re.I)
    cs_attr = re.compile(r"(colspan|rowspan)\s*=\s*(\"[^\"]*\"|'[^']*'|\S+)", re.I)

    new_tr = ""
    last_end = 0
    for td_m in td_open.finditer(row_content):
        cs_text = "".join(" " + a.group(0) for a in cs_attr.finditer(td_m.group(0)))

        td_content_start = td_m.end()
        td_end = td_close.search(row_content, td_content_start)
        cell = row_content[td_content_start:td_end.start()].strip() if td_end else row_content[td_content_start:].strip()

        new_tr += row_content[last_end:td_m.start()]
        new_tr += f'<td{cs_text} style="border: 1px solid black; padding: 8px; text-align: left;">{cell if cell else " "}</td>'

        last_end = td_end.end() if td_end else len(row_content)

    new_tr += row_content[last_end:]
    return f'<tr style="height: {rh}px;">{new_tr}</tr>'


def build_table(html):
    # 提取所有 <table>
    tables = list(re.finditer(r"<table[\s\S]*?</table>", html, re.I))
    if not tables:
        return html

    first_table = tables[0].group(0)
    cols = count_cols(first_table)
    tbl_w, tbl_h = 690.891, 928.984

    # 列宽
    col_pcts = [float(m.group(1)) for m in re.finditer(r'<col[^>]*style\s*=\s*[\'"][^\'"]*width\s*:\s*([\d.]+)%', first_table, re.I)]
    while len(col_pcts) < cols:
        col_pcts.append(100.0 / cols)
    col_px = [round(p / 100.0 * tbl_w, 4) for p in col_pcts]

    colgroup = "<colgroup>" + "".join(f'<col style="width: {w}px;">' for w in col_px) + "</colgroup>"

    # 提取标题
    titles = [(m.start(), m.group(1).strip()) for m in re.finditer(r"<p[^>]*>\s*<b[^>]*>([\s\S]*?)</b>", html, re.I)]

    tbody = ""
    last_table_end = -1
    for ti, tm in enumerate(tables):
        table_html = tm.group(0)
        table_pos = tm.start()

        # 插入此表格前的标题
        for tpos, ttext in titles:
            if last_table_end < tpos < table_pos:
                tbody += f'<tr style="height: 30px;"><td colspan="{cols}" style="border: 1px solid black; padding: 8px; text-align: left; font-weight: bold; font-size: 14px;">{ttext}</td></tr>'

        # 行高
        row_pcts = [float(m.group(1)) for m in re.finditer(r"<tr\s+[^>]*style\s*=\s*[\'\"][^\'\"]*height\s*:\s*([\d.]+)%", table_html, re.I)]
        trs = list(re.finditer(r"<tr[^>]*>([\s\S]*?)</tr>", table_html, re.I))
        for ri, trm in enumerate(trs):
            rh = row_pcts[ri] / 100.0 * tbl_h if ri < len(row_pcts) else tbl_h / max(len(trs), 1)
            tbody += process_row(trm.group(1), rh)

        last_table_end = tm.end()

    # 最后一个表格后的标题
    last_end = tables[-1].end() if tables else 0
    for tpos, ttext in titles:
        if tpos > last_end:
            tbody += f'<tr style="height: 30px;"><td colspan="{cols}" style="border: 1px solid black; padding: 8px; text-align: left; font-weight: bold; font-size: 14px;">{ttext}</td></tr>'

    return (
        f'<table style="border-collapse: collapse; table-layout: fixed; width: {tbl_w}px; height: {tbl_h}px; '
        f'color: rgb(0, 0, 0); font-family: &quot;Noto Sans SC&quot;; font-size: medium; '
        f'font-style: normal; font-variant-ligatures: normal; font-variant-caps: normal; '
        f'font-weight: 400; letter-spacing: normal; orphans: 2; text-align: start; '
        f'text-transform: none; widows: 2; word-spacing: 0px; -webkit-text-stroke-width: 0px; '
        f'white-space: normal; text-decoration-thickness: initial; text-decoration-style: initial; '
        f'text-decoration-color: initial;">'
        f'{colgroup}<tbody>{tbody}</tbody></table>'
    )


def wrap_cf_html(table_fragment):
    source_url = "file:///C:/Users/zhuyue/Desktop/RapidOcrEmbed/ocr-qt-cpp/release/output.html"
    html_body = (
        "<html>\r\n<body>\r\n<!--StartFragment-->"
        + table_fragment
        + "<!--EndFragment-->\r\n</body>\r\n</html>"
    )
    hdr_template = (
        "Version:0.9\r\n"
        "StartHTML:0000000000\r\n"
        "EndHTML:0000000000\r\n"
        "StartFragment:0000000000\r\n"
        "EndFragment:0000000000\r\n"
        f"SourceURL:{source_url}\r\n\r\n"
    )
    hdr_bytes = hdr_template.encode("utf-8")
    body_bytes = html_body.encode("utf-8")
    hdr_size = len(hdr_bytes)

    frag_marker = b"<!--StartFragment-->"
    frag_pos = body_bytes.find(frag_marker)
    start_frag = hdr_size + frag_pos + len(frag_marker)
    end_frag = hdr_size + body_bytes.find(b"<!--EndFragment-->")

    header = (
        "Version:0.9\r\n"
        f"StartHTML:{hdr_size:010d}\r\n"
        f"EndHTML:{hdr_size + len(body_bytes):010d}\r\n"
        f"StartFragment:{start_frag:010d}\r\n"
        f"EndFragment:{end_frag:010d}\r\n"
        f"SourceURL:{source_url}\r\n\r\n"
    )
    return header + html_body


def write_to_clipboard(cf_html_text):
    data = cf_html_text.encode("utf-8")
    CF_HTML = user32.RegisterClipboardFormatW("HTML Format")
    user32.OpenClipboard(None)
    user32.EmptyClipboard()

    h_mem = kernel32.GlobalAlloc(GMEM_MOVEABLE, len(data) + 1)
    ptr = kernel32.GlobalLock(h_mem)
    ctypes.memmove(ptr, data, len(data))
    ctypes.memset(ptr + len(data), 0, 1)
    kernel32.GlobalUnlock(h_mem)
    user32.SetClipboardData(CF_HTML, h_mem)
    user32.CloseClipboard()


def main():
    with open(SOURCE_HTML, "r", encoding="utf-8") as f:
        source = f.read()

    table_count = len(re.findall(r"<table", source, re.I))
    print(f"源 HTML 中有 {table_count} 个 <table>")

    table_fragment = build_table(source)
    tr_count = table_fragment.count("<tr ")
    print(f"合并后有 {tr_count} 个 <tr>")

    cf_html = wrap_cf_html(table_fragment)

    with open(OUTPUT, "w", encoding="utf-8") as f:
        f.write(cf_html)
    print(f"已保存到: {OUTPUT}")

    try:
        write_to_clipboard(cf_html)
        print("已写入剪贴板！请打开 Excel 按 Ctrl+V 测试。")
    except Exception as e:
        print(f"写入剪贴板失败: {e}")


if __name__ == "__main__":
    main()
