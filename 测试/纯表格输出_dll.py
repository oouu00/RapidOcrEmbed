# -*- coding: utf-8 -*-
"""
Table Recognition Comparison: SLANet (ONNX) vs img2table-cpp (OpenCV)
Mode 1 - SLANet: OcrInitTableEmbedded + OcrDetectTableMem (ML model)
Mode 2 - img2table: OcrDetectTable2Mem (pure OpenCV, no model)
Outputs: HTML files, CF_HTML file, and clipboard.
"""
import os
import re
import ctypes
import copy
import numpy as np
import cv2


class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int), ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float), ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float), ("doAngle", ctypes.c_int), ("mostAngle", ctypes.c_int),
    ]


# ============ MinerU-style HTML post-processing ============
# These post-processing functions (deal_eb_token, deal_bb, deal_isolate_span,
# deal_duplicate_bb) are ported from MinerU:
#   mineru/model/table/rec/slanet_plus/matcher_utils.py
# Copyright (c) Opendatalab. All rights reserved.
# Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserve.
# Licensed under Apache License 2.0.

def deal_isolate_span(thead_part):
    isolate_pattern = (
        r"<td></td> rowspan='(\d)+' colspan='(\d)+'></b></td>|"
        r"<td></td> colspan='(\d)+' rowspan='(\d)+'></b></td>|"
        r"<td></td> rowspan='(\d)+'></b></td>|"
        r"<td></td> colspan='(\d)+'></b></td>"
    )
    isolate_iter = re.finditer(isolate_pattern, thead_part)
    isolate_list = [i.group() for i in isolate_iter]
    span_pattern = (
        r" rowspan='(\d)+' colspan='(\d)+'|"
        r" colspan='(\d)+' rowspan='(\d)+'|"
        r" rowspan='(\d)+'|"
        r" colspan='(\d)+'"
    )
    for isolate_item in isolate_list:
        span_part = re.search(span_pattern, isolate_item)
        if span_part:
            corrected = f"<td{span_part.group()}></td>"
            thead_part = thead_part.replace(isolate_item, corrected)
    return thead_part


def deal_duplicate_bb(thead_part):
    td_pattern = (
        r"<td rowspan='(\d)+' colspan='(\d)+'>(.+?)</td>|"
        r"<td colspan='(\d)+' rowspan='(\d)+'>(.+?)</td>|"
        r"<td rowspan='(\d)+'>(.+?)</td>|"
        r"<td colspan='(\d)+'>(.+?)</td>|"
        r"<td>(.*?)</td>"
    )
    td_iter = re.finditer(td_pattern, thead_part)
    td_list = [t.group() for t in td_iter]
    for td_item in td_list:
        if td_item.count("<b>") > 1 or td_item.count("</b>") > 1:
            new_td = td_item.replace("<b>", "").replace("</b>", "")
            new_td = new_td.replace("<td>", "<td><b>").replace("</td>", "</b></td>")
            thead_part = thead_part.replace(td_item, new_td)
    return thead_part


def deal_bb(result_token):
    thead_pattern = "<thead>(.*?)</thead>"
    m = re.search(thead_pattern, result_token)
    if m is None:
        return result_token
    thead_part = m.group()
    origin_thead = copy.deepcopy(thead_part)
    has_span = bool(re.search(
        r"<td rowspan='(\d)+' colspan='(\d)+'>|<td colspan='(\d)+' rowspan='(\d)+'>|<td rowspan='(\d)+'>|<td colspan='(\d)+'>",
        thead_part
    ))
    if not has_span:
        thead_part = thead_part.replace("<td>", "<td><b>").replace("</td>", "</b></td>")
        thead_part = thead_part.replace("<b><b>", "<b>").replace("</b></b>", "</b>")
    else:
        span_strs = [m.group() for m in re.finditer(
            r"<td rowspan='(\d)+' colspan='(\d)+'>|<td colspan='(\d)+' rowspan='(\d)+'>|<td rowspan='(\d)+'>|<td colspan='(\d)+'>",
            thead_part
        )]
        for sp in span_strs:
            thead_part = thead_part.replace(sp, sp.replace(">", "><b>"))
        thead_part = thead_part.replace("</td>", "</b></td>")
        thead_part = re.sub(r"(<b>)+", "<b>", thead_part)
        thead_part = re.sub(r"(</b>)+", "</b>", thead_part)
        thead_part = thead_part.replace("<td>", "<td><b>").replace("<b><b>", "<b>")
    thead_part = thead_part.replace("<td><b></b></td>", "<td></td>")
    thead_part = deal_duplicate_bb(thead_part)
    thead_part = deal_isolate_span(thead_part)
    return result_token.replace(origin_thead, thead_part)


def deal_eb_token(master_token):
    replacements = {
        "<eb></eb>": "<td></td>",
        "<eb1></eb1>": "<td> </td>",
        "<eb2></eb2>": "<td><b> </b></td>",
        "<eb3></eb3>": "<td>\u2028\u2028</td>",
        "<eb4></eb4>": "<td><sup> </sup></td>",
        "<eb5></eb5>": "<td><b></b></td>",
        "<eb6></eb6>": "<td><i> </i></td>",
        "<eb7></eb7>": "<td><b><i></i></b></td>",
        "<eb8></eb8>": "<td><b><i> </i></b></td>",
        "<eb9></eb9>": "<td><i></i></td>",
        "<eb10></eb10>": "<td><b> \u2028 \u2028 </b></td>",
    }
    for old, new in replacements.items():
        master_token = master_token.replace(old, new)
    return master_token


def html_postprocess(html_str):
    html_str = deal_eb_token(html_str)
    html_str = deal_bb(html_str)
    return html_str


# ============ Main Recognizer (single DLL handle) ============

class TableRecognizerDLL:
    def __init__(self, dll_path):
        print(f"Loading DLL: {os.path.basename(dll_path)}")
        self.dll = ctypes.CDLL(dll_path)
        self._setup_dll()
        # OcrInitTableEmbedded initializes BOTH OCR and SLANet models internally
        self.h_table = self.dll.OcrInitTableEmbedded(4)
        print("Ready\n")

    def _setup_dll(self):
        d = self.dll
        d.OcrInitTableEmbedded.restype = ctypes.c_void_p
        d.OcrInitTableEmbedded.argtypes = [ctypes.c_int]
        d.OcrSetTableMode.restype = None
        d.OcrSetTableMode.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d.OcrDetectTableMem.restype = ctypes.c_int
        d.OcrDetectTableMem.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.POINTER(OCR_PARAM)]
        d.OcrGetTableLen.restype = ctypes.c_int
        d.OcrGetTableLen.argtypes = [ctypes.c_void_p]
        d.OcrGetTableResult.restype = ctypes.c_int
        d.OcrGetTableResult.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetTableStructureScore.restype = ctypes.c_float
        d.OcrGetTableStructureScore.argtypes = [ctypes.c_void_p]
        d.OcrGetTableOcrText.restype = ctypes.c_int
        d.OcrGetTableOcrText.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetTableCellCount.restype = ctypes.c_int
        d.OcrGetTableCellCount.argtypes = [ctypes.c_void_p]
        d.OcrGetTableCell.restype = ctypes.c_int
        d.OcrGetTableCell.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int), ctypes.POINTER(ctypes.c_int)]
        d.OcrDestroy.restype = None
        d.OcrDestroy.argtypes = [ctypes.c_void_p]

    def _img_to_bytes(self, img):
        _, encoded = cv2.imencode('.jpg', img)
        return encoded.tobytes()

    def recognize(self, image_path):
        img = cv2.imdecode(np.fromfile(image_path, dtype=np.uint8), cv2.IMREAD_COLOR)
        ih, iw = img.shape[:2]
        print(f"Image: {iw}x{ih}")

        print("Step 1: OCR + Table structure + text matching (DLL detectTable)...")
        data = self._img_to_bytes(img)
        param = OCR_PARAM(50, 1024, 0.5, 0.3, 2.0, 1, 1)
        self.dll.OcrDetectTableMem(
            self.h_table,
            (ctypes.c_ubyte * len(data))(*data), len(data),
            ctypes.byref(param)
        )

        tlen = self.dll.OcrGetTableLen(self.h_table)
        if tlen <= 0:
            return "", 0.0

        buf = ctypes.create_string_buffer(tlen + 1)
        self.dll.OcrGetTableResult(self.h_table, buf, tlen + 1)
        dll_html = buf.value.decode('utf-8')

        score = self.dll.OcrGetTableStructureScore(self.h_table)
        print(f"Structure score: {score:.4f}")

        return dll_html, score

    def get_cell_boxes(self):
        count = self.dll.OcrGetTableCellCount(self.h_table)
        boxes = []
        for i in range(count):
            x1 = ctypes.c_int(); y1 = ctypes.c_int()
            x2 = ctypes.c_int(); y2 = ctypes.c_int()
            self.dll.OcrGetTableCell(self.h_table, i, ctypes.byref(x1), ctypes.byref(y1), ctypes.byref(x2), ctypes.byref(y2))
            boxes.append((x1.value, y1.value, x2.value, y2.value))
        return boxes

    def draw_cell_boxes(self, img, boxes, color=(0, 0, 255), thickness=2):
        vis = img.copy()
        for (x1, y1, x2, y2) in boxes:
            cv2.rectangle(vis, (x1, y1), (x2, y2), color, thickness)
        return vis

    def destroy(self):
        if self.h_table:
            self.dll.OcrDestroy(self.h_table)


def generate_cf_html(fragment):
    html = (
        '<html>\r\n'
        '<body>\r\n'
        '<!--StartFragment-->\r\n'
        f'{fragment}\r\n'
        '<!--EndFragment-->\r\n'
        '</body>\r\n'
        '</html>'
    )
    html_bytes = html.encode('utf-8')
    hdr = (
        'Version:0.9\r\n'
        'StartHTML:{:010d}\r\n'
        'EndHTML:{:010d}\r\n'
        'StartFragment:{:010d}\r\n'
        'EndFragment:{:010d}\r\n'
    )
    hdr_size = len(hdr.format(0, 0, 0, 0).encode('utf-8'))
    start_mark = b'<!--StartFragment-->'
    end_mark = b'<!--EndFragment-->'
    sm_pos = html_bytes.index(start_mark)
    em_pos = html_bytes.index(end_mark)
    start_frag = hdr_size + sm_pos + len(start_mark)
    end_frag = hdr_size + em_pos
    start_html = hdr_size
    end_html = hdr_size + len(html_bytes)
    header = hdr.format(start_html, end_html, start_frag, end_frag)
    return header.encode('utf-8') + html_bytes


def write_cf_html_to_clipboard(data):
    import ctypes as _ctypes
    user32 = _ctypes.windll.user32
    kernel32 = _ctypes.windll.kernel32
    CF_HTML = user32.RegisterClipboardFormatW("HTML Format")
    if not user32.OpenClipboard(None):
        raise RuntimeError("无法打开剪贴板")
    try:
        user32.EmptyClipboard()
        handle = kernel32.GlobalAlloc(0x0042, len(data) + 1)
        if not handle:
            raise RuntimeError("GlobalAlloc 失败")
        ptr = kernel32.GlobalLock(handle)
        if ptr:
            _ctypes.memmove(ptr, data, len(data))
        kernel32.GlobalUnlock(handle)
        if not user32.SetClipboardData(CF_HTML, handle):
            kernel32.GlobalFree(handle)
            raise RuntimeError("SetClipboardData 失败")
    finally:
        user32.CloseClipboard()


def main():
    proj = r"C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed"
    base_dir = os.path.join(proj, "测试")
    dll_path = os.path.join(base_dir, "RapidOcrOnnx_small.dll")
    test_image = os.path.join(base_dir, "333.jpg")

    print("=" * 60)
    print("Table Recognition Comparison: SLANet vs img2table-cpp")
    print("=" * 60)

    recognizer = TableRecognizerDLL(dll_path)
    try:
        # --- Mode 1: SLANet (ONNX model) ---
        print("\n" + "=" * 60)
        print("[Mode 1] SLANet (ONNX model) + OCR")
        print("=" * 60)
        recognizer.dll.OcrSetTableMode(recognizer.h_table, 0)
        html1, score1 = recognizer.recognize(test_image)

        cf_html_data1 = generate_cf_html(html1)
        cf_html_path1 = os.path.join(base_dir, "table_slanet_cf.html")
        with open(cf_html_path1, 'wb') as f:
            f.write(cf_html_data1)
        print(f"CF_HTML saved: {cf_html_path1}")

        out_path1 = os.path.join(base_dir, "table_slanet.html")
        with open(out_path1, 'w', encoding='utf-8') as f:
            f.write(html1)
        print(f"HTML saved: {out_path1}")

        # Draw cell boxes on original image
        img_orig = cv2.imdecode(np.fromfile(test_image, dtype=np.uint8), cv2.IMREAD_COLOR)
        boxes1 = recognizer.get_cell_boxes()
        print(f"Cell boxes ({len(boxes1)}):")
        for i, b in enumerate(boxes1):
            print(f"  [{i}] ({b[0]},{b[1]})-({b[2]},{b[3]}) w={b[2]-b[0]} h={b[3]-b[1]}")
        vis1 = recognizer.draw_cell_boxes(img_orig, boxes1)
        vis_path1 = os.path.join(proj, "table_slanet_vis.jpg")
        cv2.imwrite(vis_path1, vis1)
        print(f"Visualization saved: {vis_path1}")

        # --- Mode 2: img2table-cpp (pure OpenCV) ---
        print("\n" + "=" * 60)
        print("[Mode 2] img2table-cpp (pure OpenCV, no model)")
        print("=" * 60)
        recognizer.dll.OcrSetTableMode(recognizer.h_table, 1)
        html2, score2 = recognizer.recognize(test_image)

        cf_html_data2 = generate_cf_html(html2)
        cf_html_path2 = os.path.join(base_dir, "table_img2table_cf.html")
        with open(cf_html_path2, 'wb') as f:
            f.write(cf_html_data2)
        print(f"CF_HTML saved: {cf_html_path2}")

        write_cf_html_to_clipboard(cf_html_data2)
        print("CF_HTML (img2table) written to clipboard, Ctrl+V to paste in Excel")

        out_path2 = os.path.join(base_dir, "table_img2table.html")
        with open(out_path2, 'w', encoding='utf-8') as f:
            f.write(html2)
        print(f"HTML saved: {out_path2}")

        # Draw cell boxes on original image
        img_orig2 = cv2.imdecode(np.fromfile(test_image, dtype=np.uint8), cv2.IMREAD_COLOR)
        boxes2 = recognizer.get_cell_boxes()
        print(f"Cell boxes ({len(boxes2)}):")
        for i, b in enumerate(boxes2):
            print(f"  [{i}] ({b[0]},{b[1]})-({b[2]},{b[3]}) w={b[2]-b[0]} h={b[3]-b[1]}")
        vis2 = recognizer.draw_cell_boxes(img_orig2, boxes2)
        vis_path2 = os.path.join(proj, "table_img2table_vis.jpg")
        cv2.imwrite(vis_path2, vis2)
        print(f"Visualization saved: {vis_path2}")

        # --- Comparison ---
        print("\n" + "=" * 60)
        print("Comparison Summary")
        print("=" * 60)
        print(f"[Mode 1] SLANet:    score={score1:.4f}  -> table_slanet.html")
        print(f"[Mode 2] img2table: score={score2:.4f}  -> table_img2table.html")
        print(f"Clipboard contains img2table result (Mode 2)")

    finally:
        recognizer.destroy()


if __name__ == "__main__":
    main()
