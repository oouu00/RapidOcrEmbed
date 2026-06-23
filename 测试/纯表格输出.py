# -*- coding: utf-8 -*-
"""
Table Recognition: SLANet ONNX (structure+bbox) + DLL OCR -> HTML
Uses MinerU-style IoU matching + HTML post-processing for accuracy
"""
import os
import re
import ctypes
import copy
import yaml
import numpy as np
import cv2
import onnxruntime as ort


class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int), ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float), ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float), ("doAngle", ctypes.c_int), ("mostAngle", ctypes.c_int),
    ]


# ============ SLANet ONNX inference ============

class SLANetOnnx:
    def __init__(self, onnx_path, config_path=None):
        print(f"Loading SLANet ONNX: {onnx_path}")
        self.session = ort.InferenceSession(onnx_path, providers=['CPUExecutionProvider'])
        self.input_name = self.session.get_inputs()[0].name

        self.char_dict, self.dict_map = self._load_char_dict(config_path)
        print(f"Character dict size: {len(self.char_dict)}")
        print("SLANet ONNX ready\n")

    def _load_char_dict(self, config_path):
        chars = []
        if config_path and os.path.exists(config_path):
            with open(config_path, 'r', encoding='utf-8') as f:
                config = yaml.safe_load(f)
            chars = config.get('PostProcess', {}).get('character_dict', [])

        if "<td></td>" not in chars:
            chars.append("<td></td>")
        if "<td>" in chars:
            chars.remove("<td>")

        full_chars = ["sos"] + chars + ["eos"]
        dict_map = {c: i for i, c in enumerate(full_chars)}
        return full_chars, dict_map

    def preprocess(self, img):
        h, w = img.shape[:2]
        max_len = 488
        scale = max_len / max(h, w)
        new_h, new_w = int(h * scale), int(w * scale)
        resized = cv2.resize(img, (new_w, new_h))

        resized = cv2.cvtColor(resized, cv2.COLOR_BGR2RGB).astype(np.float32) / 255.0
        mean = np.array([0.485, 0.456, 0.406], dtype=np.float32)
        std = np.array([0.229, 0.224, 0.225], dtype=np.float32)
        resized = (resized - mean) / std
        resized = resized.transpose(2, 0, 1)

        padded = np.zeros((1, 3, max_len, max_len), dtype=np.float32)
        padded[0, :, :new_h, :new_w] = resized
        return padded, (h, w), scale

    def decode(self, structure_probs, bbox_preds, orig_size, scale):
        structure_idx = structure_probs.argmax(axis=2)
        probs_max = structure_probs.max(axis=2)

        beg_idx = self.dict_map.get("sos", 0)
        end_idx = self.dict_map.get("eos", len(self.char_dict) - 1)
        td_tokens = ["<td>", "<td", "<td></td>"]

        structure_list = []
        bbox_list = []
        score_list = []

        for idx in range(structure_idx.shape[1]):
            char_idx = int(structure_idx[0, idx])
            if idx > 0 and char_idx == end_idx:
                break
            if char_idx in (beg_idx, end_idx):
                continue

            text = self.char_dict[char_idx]
            structure_list.append(text)
            score_list.append(probs_max[0, idx])

            if text in td_tokens:
                bbox = bbox_preds[0, idx].copy()
                bbox[0::2] *= orig_size[1]
                bbox[1::2] *= orig_size[0]
                bbox_list.append(bbox.tolist())

        avg_score = float(np.mean(score_list)) if score_list else 0.0
        return structure_list, np.array(bbox_list, dtype=np.float64), avg_score

    def predict(self, img):
        input_tensor, orig_size, scale = self.preprocess(img)
        outputs = self.session.run(None, {self.input_name: input_tensor})
        bbox_preds = outputs[0]
        structure_probs = outputs[1]
        return self.decode(structure_probs, bbox_preds, orig_size, scale)


# ============ MinerU-style matching helpers ============
# Ported from MinerU:
#   mineru/model/table/rec/slanet_plus/matcher.py
#   mineru/model/table/rec/slanet_plus/matcher_utils.py
# Copyright (c) Opendatalab. All rights reserved.
# Copyright (c) 2022 PaddlePaddle Authors. All Rights Reserve.
# Licensed under Apache License 2.0.

def normalize_cell_bboxes(cell_bboxes):
    if cell_bboxes is None or len(cell_bboxes) == 0:
        return np.empty((0, 4), dtype=np.float64)
    arr = np.asarray(cell_bboxes, dtype=np.float64)
    if arr.ndim == 2 and arr.shape[1] == 8:
        x_coords = arr[:, 0::2]
        y_coords = arr[:, 1::2]
        return np.stack([
            np.min(x_coords, axis=1),
            np.min(y_coords, axis=1),
            np.max(x_coords, axis=1),
            np.max(y_coords, axis=1),
        ], axis=1).astype(np.float64)
    return arr


def pairwise_iou_and_distance(dt_boxes, cell_bboxes):
    dt = dt_boxes[:, None, :]
    cells = cell_bboxes[None, :, :]
    dt_area = (dt[..., 2] - dt[..., 0]) * (dt[..., 3] - dt[..., 1])
    cell_area = (cells[..., 2] - cells[..., 0]) * (cells[..., 3] - cells[..., 1])
    sum_area = dt_area + cell_area
    left_line = np.maximum(dt[..., 1], cells[..., 1])
    right_line = np.minimum(dt[..., 3], cells[..., 3])
    top_line = np.maximum(dt[..., 0], cells[..., 0])
    bottom_line = np.minimum(dt[..., 2], cells[..., 2])
    intersect = (right_line - left_line) * (bottom_line - top_line)
    has_intersection = (left_line < right_line) & (top_line < bottom_line)
    union = sum_area - intersect
    iou = np.zeros_like(intersect, dtype=np.float64)
    np.divide(intersect, union, out=iou, where=has_intersection & (union != 0))
    dis = (
        np.abs(cells[..., 0] - dt[..., 0])
        + np.abs(cells[..., 1] - dt[..., 1])
        + np.abs(cells[..., 2] - dt[..., 2])
        + np.abs(cells[..., 3] - dt[..., 3])
    )
    dis_2 = np.abs(cells[..., 0] - dt[..., 0]) + np.abs(cells[..., 1] - dt[..., 1])
    dis_3 = np.abs(cells[..., 2] - dt[..., 2]) + np.abs(cells[..., 3] - dt[..., 3])
    distance_score = dis + np.minimum(dis_2, dis_3)
    return iou, distance_score


def select_best_cell_indices(iou_scores, distance_scores):
    best_indices = []
    inverse_iou = 1.0 - iou_scores
    for row in range(inverse_iou.shape[0]):
        min_iou = np.min(inverse_iou[row])
        candidates = np.flatnonzero(inverse_iou[row] == min_iou)
        candidate_dists = distance_scores[row, candidates]
        min_dist = np.min(candidate_dists)
        best = candidates[np.flatnonzero(candidate_dists == min_dist)[0]]
        best_indices.append(int(best))
    return best_indices


def match_result(dt_boxes, cell_bboxes, min_iou=1e-8):
    matched = {}
    dt_boxes = np.asarray(dt_boxes, dtype=np.float64).reshape(-1, 4)
    cell_bboxes = normalize_cell_bboxes(cell_bboxes)
    if dt_boxes.size == 0 or cell_bboxes.size == 0:
        return matched
    iou_scores, distance_scores = pairwise_iou_and_distance(dt_boxes, cell_bboxes)
    best_indices = select_best_cell_indices(iou_scores, distance_scores)
    for ocr_idx, cell_idx in enumerate(best_indices):
        if 1.0 - iou_scores[ocr_idx, cell_idx] >= 1 - min_iou:
            continue
        matched.setdefault(cell_idx, []).append(ocr_idx)
    return matched


# ============ MinerU-style HTML post-processing ============
# Ported from MinerU:
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


# ============ Main Recognizer ============

class TableRecognizer:
    def __init__(self, dll_path, onnx_path, config_path=None):
        self.slanet = SLANetOnnx(onnx_path, config_path)

        print(f"Loading DLL: {os.path.basename(dll_path)}")
        self.dll = ctypes.CDLL(dll_path)
        self._setup_dll()
        self.h = self.dll.OcrInitEmbedded(4)
        print("Ready\n")

    def _setup_dll(self):
        d = self.dll
        d.OcrInitEmbedded.restype = ctypes.c_void_p
        d.OcrInitEmbedded.argtypes = [ctypes.c_int]
        d.OcrDetectMemEx.restype = ctypes.c_int
        d.OcrDetectMemEx.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.POINTER(OCR_PARAM)]
        d.OcrGetBlockCount.restype = ctypes.c_int
        d.OcrGetBlockCount.argtypes = [ctypes.c_void_p]
        d.OcrGetBlockText.restype = ctypes.c_int
        d.OcrGetBlockText.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetBlockBox.restype = ctypes.c_int
        d.OcrGetBlockBox.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        d.OcrGetBlockScore.restype = ctypes.c_float
        d.OcrGetBlockScore.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d.OcrDestroy.restype = None
        d.OcrDestroy.argtypes = [ctypes.c_void_p]

    def ocr_full_image(self, img):
        _, encoded = cv2.imencode('.jpg', img)
        data = encoded.tobytes()
        param = OCR_PARAM(50, 1024, 0.5, 0.3, 2.0, 1, 1)
        self.dll.OcrDetectMemEx(self.h, (ctypes.c_ubyte * len(data))(*data), len(data), ctypes.byref(param))
        count = self.dll.OcrGetBlockCount(self.h)
        blocks = []
        for i in range(count):
            score = self.dll.OcrGetBlockScore(self.h, i)
            if score < 0.3:
                continue
            tlen = self.dll.OcrGetBlockText(self.h, i, None, 0)
            if tlen <= 0:
                continue
            buf = ctypes.create_string_buffer(tlen + 1)
            self.dll.OcrGetBlockText(self.h, i, buf, tlen + 1)
            text = buf.value.decode('utf-8').strip()
            if not text:
                continue
            bbox_buf = (ctypes.c_int * 8)()
            self.dll.OcrGetBlockBox(self.h, i, bbox_buf)
            xs = [bbox_buf[j * 2] for j in range(4)]
            ys = [bbox_buf[j * 2 + 1] for j in range(4)]
            cx = sum(xs) / 4.0
            cy = sum(ys) / 4.0
            box = [min(xs), min(ys), max(xs), max(ys)]
            blocks.append({'text': text, 'cx': cx, 'cy': cy, 'box': box})
        return blocks

    def filter_ocr_above_table(self, ocr_blocks, cell_bboxes):
        if cell_bboxes is None or len(cell_bboxes) == 0:
            return ocr_blocks
        cell_arr = np.asarray(cell_bboxes, dtype=np.float64)
        y1 = cell_arr[:, 1::2].min()
        return [b for b in ocr_blocks if b['box'][3] >= y1]

    def match_ocr_to_cells(self, cell_bboxes, ocr_blocks):
        if not ocr_blocks:
            return [''] * len(cell_bboxes)
        ocr_blocks = self.filter_ocr_above_table(ocr_blocks, cell_bboxes)
        dt_boxes = np.array([b['box'] for b in ocr_blocks], dtype=np.float64)
        matched = match_result(dt_boxes, cell_bboxes)
        cell_texts = [''] * len(cell_bboxes)
        for cell_idx, ocr_indices in matched.items():
            if cell_idx >= len(cell_texts):
                continue
            texts = [ocr_blocks[oi]['text'] for oi in ocr_indices if oi < len(ocr_blocks)]
            cell_texts[cell_idx] = ' '.join(texts)
        return cell_texts

    def build_html_from_pred(self, pred_tokens, cell_texts):
        end_html = []
        td_index = 0
        for tag in pred_tokens:
            if "</td>" not in tag:
                end_html.append(tag)
                continue
            if "<td></td>" == tag:
                end_html.append("<td>")
            if td_index < len(cell_texts) and cell_texts[td_index]:
                end_html.append(cell_texts[td_index])
            if "<td></td>" == tag:
                end_html.append("</td>")
            else:
                end_html.append(tag)
            td_index += 1

        filter_elements = ["<thead>", "</thead>", "<tbody>", "</tbody>"]
        end_html = [v for v in end_html if v not in filter_elements]
        html_str = "".join(end_html)
        if not html_str.startswith("<table>"):
            html_str = "<table>" + html_str
        if not html_str.endswith("</table>"):
            html_str = html_str + "</table>"
        html_str = html_postprocess(html_str)
        return html_str

    def recognize(self, image_path):
        img = cv2.imdecode(np.fromfile(image_path, dtype=np.uint8), cv2.IMREAD_COLOR)
        ih, iw = img.shape[:2]
        print(f"Image: {iw}x{ih}")

        print("Step 1: Table structure recognition (SLANet ONNX)...")
        pred_struct, cell_bboxes, structure_score = self.slanet.predict(img)

        print(f"Structure score: {structure_score:.4f}")
        print(f"Cell bboxes: {len(cell_bboxes)}")
        print(f"Structure tokens: {len(pred_struct)}")

        print("Step 2: OCR full image...")
        ocr_blocks = self.ocr_full_image(img)
        print(f"OCR blocks: {len(ocr_blocks)}")
        for b in ocr_blocks:
            print(f"  [{int(b['cx']):4d},{int(b['cy']):4d}]  {b['text']}")

        print("Step 3: Match OCR to cells (MinerU IoU matching)...")
        cell_texts = self.match_ocr_to_cells(cell_bboxes, ocr_blocks)

        print("Step 4: Build HTML with post-processing...")
        html = self.build_html_from_pred(pred_struct, cell_texts)

        return html, cell_bboxes, cell_texts

    def destroy(self):
        if self.h:
            self.dll.OcrDestroy(self.h)


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
    import ctypes
    user32 = ctypes.windll.user32
    kernel32 = ctypes.windll.kernel32
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
            ctypes.memmove(ptr, data, len(data))
        kernel32.GlobalUnlock(handle)
        if not user32.SetClipboardData(CF_HTML, handle):
            kernel32.GlobalFree(handle)
            raise RuntimeError("SetClipboardData 失败")
    finally:
        user32.CloseClipboard()


def main():
    proj = r"C:\Users\zhuyue\Desktop\RapidOcrEmbed\RapidOcrEmbed"
    base_dir = os.path.join(proj, "测试")
    dll_path = os.path.join(proj, "output", "RapidOcrOnnx_small.dll")
    onnx_path = os.path.join(proj, "模型转二进制", "models", "SLANet_onnx", "inference.onnx")
    config_path = os.path.join(proj, "模型转二进制", "models", "SLANet_onnx", "inference.yml")
    test_image = os.path.join(base_dir, "333.jpg")

    print("=" * 60)
    print("Table Recognition: SLANet ONNX + DLL OCR -> HTML")
    print("(MinerU-style IoU matching + HTML post-processing)")
    print("=" * 60)

    recognizer = TableRecognizer(dll_path, onnx_path, config_path)
    try:
        html, cell_bboxes, cell_texts = recognizer.recognize(test_image)

        fragment = html.strip()
        fragment = fragment.replace(
            '<table>',
            '<table border="1" cellpadding="4" style="border-collapse:collapse">'
        )

        cf_html_data = generate_cf_html(fragment)
        cf_html_path = os.path.join(base_dir, "table_for_excel_cf.html")
        with open(cf_html_path, 'wb') as f:
            f.write(cf_html_data)
        print(f"\nCF_HTML saved: {cf_html_path}")

        write_cf_html_to_clipboard(cf_html_data)
        print("CF_HTML written to clipboard, Ctrl+V to paste in Excel")

        html_out = (
            '<html>\n<head><meta charset="UTF-8">\n'
            '<style>table{border-collapse:collapse;width:auto}td,th{border:1px solid black;padding:8px;text-align:left}</style>\n'
            '</head>\n<body>\n'
            f'{fragment}\n'
            '</body>\n</html>'
        )
        out_path = os.path.join(base_dir, "table_for_excel.html")
        with open(out_path, 'w', encoding='utf-8') as f:
            f.write(html_out)
        print(f"HTML saved: {out_path}")
    finally:
        recognizer.destroy()


if __name__ == "__main__":
    main()
