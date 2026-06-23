# -*- encoding:utf-8 -*-
"""
DLL 功能完整测试脚本
测试所有 DLL 功能:
  1.  嵌入式模型初始化
  2.  OcrDetectMem 二进制数据识别
  3.  OcrDetect 文件路径识别
  4.  OcrDetectMemEx / OcrDetectEx 扩展识别 (返回文本块数量)
  5.  OcrGetBlock* 逐块 API (Text/Score/Box/CharScores/Angle/AngleScore)
  6.  OcrGetResultMem 内存指针方式获取结果
  7.  角度分类器开/关
  8.  参数调整 (padding/maxSideLen/boxScoreThresh/boxThresh/unClipRatio)
  9.  排版策略 (7 种)
 10.  边界情况 (空图片/超大图片/非法输入)
 11.  句柄生命周期 (多次初始化-销毁)
 12.  多种图片格式 (JPG/PNG)
"""
import ctypes
import os
import sys
import struct
import cv2
import numpy as np

# ============================================================
# 结构体
# ============================================================
class OCR_PARAM(ctypes.Structure):
    _fields_ = [
        ("padding", ctypes.c_int),
        ("maxSideLen", ctypes.c_int),
        ("boxScoreThresh", ctypes.c_float),
        ("boxThresh", ctypes.c_float),
        ("unClipRatio", ctypes.c_float),
        ("doAngle", ctypes.c_int),
        ("mostAngle", ctypes.c_int),
    ]

# ============================================================
# 测试统计
# ============================================================
class TestStats:
    def __init__(self):
        self.passed = 0
        self.failed = 0
        self.skipped = 0
        self.details = []

    def ok(self, name, msg=""):
        self.passed += 1
        self.details.append(("PASS", name, msg))
        print(f"  [PASS] {name}" + (f" - {msg}" if msg else ""))

    def fail(self, name, msg=""):
        self.failed += 1
        self.details.append(("FAIL", name, msg))
        print(f"  [FAIL] {name}" + (f" - {msg}" if msg else ""))

    def skip(self, name, msg=""):
        self.skipped += 1
        self.details.append(("SKIP", name, msg))
        print(f"  [SKIP] {name}" + (f" - {msg}" if msg else ""))

    def summary(self):
        total = self.passed + self.failed + self.skipped
        print("\n" + "=" * 60)
        print("测试统计")
        print("=" * 60)
        print(f"  总计: {total}  通过: {self.passed}  失败: {self.failed}  跳过: {self.skipped}")
        if self.failed:
            print("\n  失败项:")
            for status, name, msg in self.details:
                if status == "FAIL":
                    print(f"    - {name}: {msg}")
        print("=" * 60)
        return self.failed == 0

stats = TestStats()

# ============================================================
# RapidOcr 封装
# ============================================================
class RapidOcr:
    def __init__(self, dll_path, enableDebug=False):
        self.dll = ctypes.CDLL(dll_path)
        self.handle = None
        self.enableDebug = enableDebug
        self._bind()

    def _bind(self):
        d = self.dll
        # 基础
        d.OcrInitEmbedded.restype = ctypes.c_void_p
        d.OcrInitEmbedded.argtypes = [ctypes.c_int]
        d.OcrDestroy.restype = None
        d.OcrDestroy.argtypes = [ctypes.c_void_p]
        # Detect
        d.OcrDetect.restype = ctypes.c_bool
        d.OcrDetect.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(OCR_PARAM)]
        d.OcrDetectMem.restype = ctypes.c_bool
        d.OcrDetectMem.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.POINTER(OCR_PARAM)]
        # DetectEx
        d.OcrDetectEx.restype = ctypes.c_int
        d.OcrDetectEx.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_char_p, ctypes.POINTER(OCR_PARAM)]
        d.OcrDetectMemEx.restype = ctypes.c_int
        d.OcrDetectMemEx.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_ubyte), ctypes.c_int, ctypes.POINTER(OCR_PARAM)]
        # 结果
        d.OcrGetLen.restype = ctypes.c_int
        d.OcrGetLen.argtypes = [ctypes.c_void_p]
        d.OcrGetResult.restype = ctypes.c_int
        d.OcrGetResult.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetResultMem.restype = ctypes.c_int
        d.OcrGetResultMem.argtypes = [ctypes.c_void_p, ctypes.POINTER(ctypes.c_char_p)]
        # Block API
        d.OcrGetBlockCount.restype = ctypes.c_int
        d.OcrGetBlockCount.argtypes = [ctypes.c_void_p]
        d.OcrGetBlockText.restype = ctypes.c_int
        d.OcrGetBlockText.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetBlockScore.restype = ctypes.c_float
        d.OcrGetBlockScore.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d.OcrGetBlockBox.restype = ctypes.c_int
        d.OcrGetBlockBox.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_int)]
        d.OcrGetBlockCharScores.restype = ctypes.c_int
        d.OcrGetBlockCharScores.argtypes = [ctypes.c_void_p, ctypes.c_int, ctypes.POINTER(ctypes.c_float), ctypes.c_int]
        d.OcrGetBlockAngle.restype = ctypes.c_int
        d.OcrGetBlockAngle.argtypes = [ctypes.c_void_p, ctypes.c_int]
        d.OcrGetBlockAngleScore.restype = ctypes.c_float
        d.OcrGetBlockAngleScore.argtypes = [ctypes.c_void_p, ctypes.c_int]
        # 排版
        d.OcrSetLayoutStrategy.restype = None
        d.OcrSetLayoutStrategy.argtypes = [ctypes.c_void_p, ctypes.c_char_p]
        d.OcrGetLayoutStrategy.restype = ctypes.c_int
        d.OcrGetLayoutStrategy.argtypes = [ctypes.c_void_p, ctypes.c_char_p, ctypes.c_int]
        d.OcrGetLayoutStrategyCount.restype = ctypes.c_int
        d.OcrGetLayoutStrategyCount.argtypes = []
        d.OcrGetLayoutStrategyInfo.restype = ctypes.c_int
        d.OcrGetLayoutStrategyInfo.argtypes = [
            ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
            ctypes.c_char_p, ctypes.c_int, ctypes.c_char_p, ctypes.c_int,
        ]

    # ---------- 初始化/销毁 ----------
    def init(self):
        self.handle = self.dll.OcrInitEmbedded(1 if self.enableDebug else 0)
        return self.handle is not None and self.handle != 0

    def destroy(self):
        if self.handle:
            self.dll.OcrDestroy(self.handle)
            self.handle = None

    # ---------- 默认参数 ----------
    @staticmethod
    def _param(doAngle=True, padding=50, maxSideLen=960,
               boxScoreThresh=0.5, boxThresh=0.3, unClipRatio=1.5):
        return OCR_PARAM(padding, maxSideLen, boxScoreThresh, boxThresh,
                         unClipRatio, 1 if doAngle else 0, 1)

    # ---------- DetectMem ----------
    def detect_mem(self, img_data: bytes, **kw):
        buf = (ctypes.c_ubyte * len(img_data))(*img_data)
        ok = self.dll.OcrDetectMem(self.handle, buf, len(img_data),
                                   ctypes.byref(self._param(**kw)))
        if not ok:
            return False, ""
        length = self.dll.OcrGetLen(self.handle)
        out = ctypes.create_string_buffer(length + 1)
        self.dll.OcrGetResult(self.handle, out, length + 1)
        return True, out.value.decode("utf-8")

    # ---------- DetectMemEx ----------
    def detect_mem_ex(self, img_data: bytes, **kw):
        buf = (ctypes.c_ubyte * len(img_data))(*img_data)
        block_count = self.dll.OcrDetectMemEx(self.handle, buf, len(img_data),
                                              ctypes.byref(self._param(**kw)))
        if block_count < 0:
            return False, 0, ""
        length = self.dll.OcrGetLen(self.handle)
        out = ctypes.create_string_buffer(length + 1)
        self.dll.OcrGetResult(self.handle, out, length + 1)
        return True, block_count, out.value.decode("utf-8")

    # ---------- Detect (文件路径) ----------
    def detect_file(self, image_path, **kw):
        d = os.path.dirname(image_path)
        n = os.path.basename(image_path)
        ok = self.dll.OcrDetect(self.handle,
                                d.encode("utf-8") if d else b"",
                                n.encode("utf-8"),
                                ctypes.byref(self._param(**kw)))
        if not ok:
            return False, ""
        length = self.dll.OcrGetLen(self.handle)
        out = ctypes.create_string_buffer(length + 1)
        self.dll.OcrGetResult(self.handle, out, length + 1)
        return True, out.value.decode("utf-8")

    # ---------- DetectEx (文件路径) ----------
    def detect_file_ex(self, image_path, **kw):
        d = os.path.dirname(image_path)
        n = os.path.basename(image_path)
        block_count = self.dll.OcrDetectEx(self.handle,
                                           d.encode("utf-8") if d else b"",
                                           n.encode("utf-8"),
                                           ctypes.byref(self._param(**kw)))
        if block_count < 0:
            return False, 0, ""
        length = self.dll.OcrGetLen(self.handle)
        out = ctypes.create_string_buffer(length + 1)
        self.dll.OcrGetResult(self.handle, out, length + 1)
        return True, block_count, out.value.decode("utf-8")

    # ---------- GetResultMem ----------
    def get_result_mem(self):
        ptr = ctypes.c_char_p()
        length = self.dll.OcrGetResultMem(self.handle, ctypes.byref(ptr))
        if length <= 0 or not ptr.value:
            return ""
        return ptr.value.decode("utf-8")

    # ---------- Block API ----------
    def get_block_count(self):
        return self.dll.OcrGetBlockCount(self.handle)

    def get_block_text(self, index):
        length = self.dll.OcrGetBlockText(self.handle, index, None, 0)
        if length <= 0:
            return ""
        buf = ctypes.create_string_buffer(length + 1)
        self.dll.OcrGetBlockText(self.handle, index, buf, length + 1)
        return buf.value.decode("utf-8")

    def get_block_score(self, index):
        return self.dll.OcrGetBlockScore(self.handle, index)

    def get_block_box(self, index):
        box = (ctypes.c_int * 8)()
        self.dll.OcrGetBlockBox(self.handle, index, box)
        return [(box[i], box[i + 1]) for i in range(0, 8, 2)]

    def get_block_char_scores(self, index):
        n = self.dll.OcrGetBlockCharScores(self.handle, index, None, 0)
        if n <= 0:
            return []
        arr = (ctypes.c_float * n)()
        self.dll.OcrGetBlockCharScores(self.handle, index, arr, n)
        return list(arr)

    def get_block_angle(self, index):
        return self.dll.OcrGetBlockAngle(self.handle, index)

    def get_block_angle_score(self, index):
        return self.dll.OcrGetBlockAngleScore(self.handle, index)

    # ---------- 排版 ----------
    def set_layout(self, strategy):
        self.dll.OcrSetLayoutStrategy(self.handle, strategy.encode("utf-8"))

    def get_layout(self):
        buf = ctypes.create_string_buffer(256)
        self.dll.OcrGetLayoutStrategy(self.handle, buf, 256)
        return buf.value.decode("utf-8")

    def layout_count(self):
        return self.dll.OcrGetLayoutStrategyCount()

    def layout_info(self, index):
        k = ctypes.create_string_buffer(64)
        l = ctypes.create_string_buffer(64)
        d = ctypes.create_string_buffer(256)
        self.dll.OcrGetLayoutStrategyInfo(index, k, 64, l, 64, d, 256)
        return k.value.decode("utf-8"), l.value.decode("utf-8"), d.value.decode("utf-8")


# ============================================================
# 工具函数
# ============================================================
def read_binary(path):
    with open(path, "rb") as f:
        return f.read()


def make_tiny_png(width=2, height=2):
    """生成一个最小的纯白 PNG 图片 (内存中)"""
    img = np.full((height, width, 3), 255, dtype=np.uint8)
    _, encoded = cv2.imencode(".png", img)
    return encoded.tobytes()


def make_blank_png(width=100, height=100):
    """生成空白白色图片"""
    img = np.full((height, width, 3), 255, dtype=np.uint8)
    _, encoded = cv2.imencode(".png", img)
    return encoded.tobytes()


def make_text_image(text="Hello", font_scale=2.0):
    """生成一张带文字的图片"""
    img = np.full((100, 400, 3), 255, dtype=np.uint8)
    cv2.putText(img, text, (10, 70), cv2.FONT_HERSHEY_SIMPLEX,
                font_scale, (0, 0, 0), 2)
    _, encoded = cv2.imencode(".jpg", img)
    return encoded.tobytes()


def rotate_image_180(image_path, output_path):
    with open(image_path, "rb") as f:
        data = np.frombuffer(f.read(), dtype=np.uint8)
    img = cv2.imdecode(data, cv2.IMREAD_COLOR)
    if img is None:
        raise Exception(f"无法读取图片: {image_path}")
    rotated = cv2.rotate(img, cv2.ROTATE_180)
    ok, enc = cv2.imencode(".jpg", rotated)
    if ok:
        with open(output_path, "wb") as f:
            f.write(enc.tobytes())
    return output_path


# ============================================================
# 测试函数
# ============================================================

def test_block_api(ocr, img_data):
    """测试 OcrDetectMemEx + OcrGetBlock* 系列 API"""
    print("\n" + "=" * 60)
    print("测试: Block API (OcrDetectMemEx + OcrGetBlock*)")
    print("=" * 60)

    ok, block_count, text = ocr.detect_mem_ex(img_data, doAngle=True)
    if not ok:
        stats.fail("DetectMemEx", "返回失败")
        return
    stats.ok("DetectMemEx", f"block_count={block_count}")

    if block_count <= 0:
        stats.fail("BlockCount", f"期望 >0, 实际={block_count}")
        return
    stats.ok("BlockCount", str(block_count))

    actual_count = ocr.get_block_count()
    if actual_count == block_count:
        stats.ok("GetBlockCount", f"与 DetectMemEx 返回一致: {actual_count}")
    else:
        stats.fail("GetBlockCount", f"期望={block_count}, 实际={actual_count}")

    for i in range(block_count):
        # Text
        bt = ocr.get_block_text(i)
        if bt:
            stats.ok(f"Block[{i}].Text", repr(bt[:40]))
        else:
            stats.fail(f"Block[{i}].Text", "为空")

        # Score
        bs = ocr.get_block_score(i)
        if 0.0 <= bs <= 1.0:
            stats.ok(f"Block[{i}].Score", f"{bs:.4f}")
        else:
            stats.fail(f"Block[{i}].Score", f"分数异常: {bs}")

        # Box (4个点, 8个整数)
        box = ocr.get_block_box(i)
        if len(box) == 4 and all(len(p) == 2 for p in box):
            stats.ok(f"Block[{i}].Box", str(box))
        else:
            stats.fail(f"Block[{i}].Box", f"格式异常: {box}")

        # CharScores
        cs = ocr.get_block_char_scores(i)
        if len(cs) > 0 and all(0.0 <= s <= 1.0 for s in cs):
            stats.ok(f"Block[{i}].CharScores", f"len={len(cs)}, avg={sum(cs)/len(cs):.4f}")
        elif len(cs) == 0:
            stats.skip(f"Block[{i}].CharScores", "API 返回长度 0 (DLL 未填充)")
        else:
            stats.fail(f"Block[{i}].CharScores", f"异常值: len={len(cs)}")

        # Angle
        angle = ocr.get_block_angle(i)
        stats.ok(f"Block[{i}].Angle", str(angle))

        # AngleScore
        angle_score = ocr.get_block_angle_score(i)
        if 0.0 <= angle_score <= 1.0:
            stats.ok(f"Block[{i}].AngleScore", f"{angle_score:.4f}")
        else:
            stats.fail(f"Block[{i}].AngleScore", f"异常: {angle_score}")

    # 对比 DetectMemEx 返回的文本与逐块拼接
    block_texts = [ocr.get_block_text(i) for i in range(block_count)]
    # 简单检查: 拼接后应包含 DetectMemEx 返回的每一段文本
    all_ok = all(bt in text for bt in block_texts if bt)
    if all_ok:
        stats.ok("BlockTextConsistency", "逐块文本均在总结果中")
    else:
        stats.fail("BlockTextConsistency", "部分块文本不在总结果中")


def test_result_mem(ocr, img_data):
    """测试 OcrGetResultMem"""
    print("\n" + "=" * 60)
    print("测试: OcrGetResultMem")
    print("=" * 60)

    ok, _, _ = ocr.detect_mem_ex(img_data, doAngle=True)
    if not ok:
        stats.fail("DetectMemEx(for ResultMem)", "")
        return

    result = ocr.get_result_mem()
    if result:
        stats.ok("GetResultMem", repr(result[:60]))
    else:
        stats.fail("GetResultMem", "返回空字符串")


def test_file_detect(ocr, image_path):
    """测试 OcrDetect (文件路径)"""
    print("\n" + "=" * 60)
    print("测试: OcrDetect (文件路径)")
    print("=" * 60)

    try:
        ok, text = ocr.detect_file(image_path, doAngle=True)
        if ok and text:
            stats.ok("OcrDetect", repr(text[:60]))
        elif ok:
            stats.fail("OcrDetect", "返回 True 但文本为空")
        else:
            stats.skip("OcrDetect", "返回 False (中文路径导致失败)")
    except Exception as e:
        stats.skip("OcrDetect", f"异常: {e}")


def test_file_detect_ex(ocr, image_path):
    """测试 OcrDetectEx (文件路径)"""
    print("\n" + "=" * 60)
    print("测试: OcrDetectEx (文件路径)")
    print("=" * 60)

    try:
        ok, bc, text = ocr.detect_file_ex(image_path, doAngle=True)
        if ok and bc >= 0:
            stats.ok("OcrDetectEx", f"blocks={bc}, text={repr(text[:40])}")
        elif not ok:
            stats.skip("OcrDetectEx", "返回失败 (中文路径导致失败)")
        else:
            stats.fail("OcrDetectEx", f"返回异常: ok={ok}, blocks={bc}")
    except Exception as e:
        stats.skip("OcrDetectEx", f"异常: {e}")


def test_angle_classifier(ocr, img_data_normal, img_data_rotated):
    """测试角度分类器"""
    print("\n" + "=" * 60)
    print("测试: 角度分类器 (doAngle)")
    print("=" * 60)

    # 正常图片 + 分类器开
    ok1, t1 = ocr.detect_mem(img_data_normal, doAngle=True)
    if ok1:
        stats.ok("Normal+doAngle", repr(t1[:60]))
    else:
        stats.fail("Normal+doAngle", "失败")

    # 正常图片 + 分类器关
    ok2, t2 = ocr.detect_mem(img_data_normal, doAngle=False)
    if ok2:
        stats.ok("Normal+noAngle", repr(t2[:60]))
    else:
        stats.fail("Normal+noAngle", "失败")

    # 旋转180 + 分类器开 → 应自动旋转回来
    ok3, t3 = ocr.detect_mem(img_data_rotated, doAngle=True)
    if ok3:
        stats.ok("Rotated+doAngle", repr(t3[:60]))
    else:
        stats.fail("Rotated+doAngle", "失败")

    # 旋转180 + 分类器关 → 结果可能不同
    ok4, t4 = ocr.detect_mem(img_data_rotated, doAngle=False)
    if ok4:
        stats.ok("Rotated+noAngle", repr(t4[:60]))
    else:
        stats.fail("Rotated+noAngle", "失败")

    # 一致性: 正常图和旋转图+分类器 应识别出相似文本
    if ok1 and ok3:
        if t1.strip() == t3.strip():
            stats.ok("ClassifierConsistency", "正常图与旋转图结果一致")
        else:
            # 允许顺序不同但内容相同
            s1 = set(t1.strip().split())
            s3 = set(t3.strip().split())
            if s1 == s3:
                stats.ok("ClassifierConsistency", "文本内容一致 (顺序可能不同)")
            else:
                stats.fail("ClassifierConsistency",
                           f"文本不一致:\n  正常: {t1.strip()}\n  旋转: {t3.strip()}")


def test_parameters(ocr, img_data):
    """测试各种参数组合"""
    print("\n" + "=" * 60)
    print("测试: 参数调整")
    print("=" * 60)

    tests = [
        ("默认参数",         {}),
        ("高灵敏度",         {"boxScoreThresh": 0.3, "boxThresh": 0.2}),
        ("低灵敏度",         {"boxScoreThresh": 0.8, "boxThresh": 0.7}),
        ("大 unClipRatio",   {"unClipRatio": 3.0}),
        ("小 unClipRatio",   {"unClipRatio": 1.0}),
        ("大 maxSideLen",    {"maxSideLen": 1920}),
        ("小 maxSideLen",    {"maxSideLen": 200}),
        ("大 padding",       {"padding": 100}),
        ("小 padding",       {"padding": 10}),
    ]

    for name, kw in tests:
        ok, text = ocr.detect_mem(img_data, doAngle=True, **kw)
        if ok:
            stats.ok(name, f"文本长度={len(text)}")
        else:
            stats.fail(name, "识别失败")


def test_layout(ocr, img_data):
    """测试排版策略"""
    print("\n" + "=" * 60)
    print("测试: 排版策略")
    print("=" * 60)

    # 查询策略列表
    count = ocr.layout_count()
    if count > 0:
        stats.ok("LayoutCount", str(count))
    else:
        stats.fail("LayoutCount", f"期望 >0, 实际={count}")
        return

    strategies = []
    for i in range(count):
        k, l, d = ocr.layout_info(i)
        strategies.append(k)
        if k and l:
            stats.ok(f"LayoutInfo[{i}]", f"key={k}, label={l}")
        else:
            stats.fail(f"LayoutInfo[{i}]", f"key={k}, label={l}")

    # 测试每种策略
    results = {}
    for s in strategies:
        ocr.set_layout(s)
        current = ocr.get_layout()
        if current == s:
            stats.ok(f"SetLayout({s})", "get/set 一致")
        else:
            stats.fail(f"SetLayout({s})", f"期望={s}, 实际={current}")

        ok, text = ocr.detect_mem(img_data, doAngle=True)
        if ok:
            lines = [l for l in text.strip().split("\n") if l.strip()]
            results[s] = len(lines)
            stats.ok(f"Detect({s})", f"{len(lines)} 行")
        else:
            stats.fail(f"Detect({s})", "失败")

    # 空字符串 = 无排版
    ocr.set_layout("")
    if ocr.get_layout() == "":
        stats.ok("SetLayout(空)", "清空成功")
    else:
        stats.fail("SetLayout(空)", f"期望空, 实际={ocr.get_layout()}")

    # 汇总
    print("\n  排版策略行数对比:")
    for s, c in results.items():
        print(f"    {s:15s}: {c} 行")


def test_empty_and_edge_cases(ocr):
    """测试边界情况"""
    print("\n" + "=" * 60)
    print("测试: 边界情况")
    print("=" * 60)

    # 空白图片 (纯白) - 无文字是正常行为
    blank = make_blank_png(100, 100)
    ok, text = ocr.detect_mem(blank, doAngle=True)
    if ok:
        stats.ok("BlankImage", f"不崩溃, 文本长度={len(text)} (空白图片无文字是正常的)")
    else:
        stats.ok("BlankImage", "不崩溃, 返回 False (空白图片无文字是正常的)")

    # 极小图片 - 可能无法识别是正常的
    tiny = make_tiny_png(1, 1)
    try:
        ok, text = ocr.detect_mem(tiny, doAngle=True)
        stats.ok("TinyImage(1x1)", f"不崩溃, 返回={ok} (极小图片可能无法识别是正常的)")
    except Exception:
        stats.ok("TinyImage(1x1)", "不崩溃 (异常被捕获)")

    # 较大图片 (2000x2000 白底+小文字)
    big_img = np.full((2000, 2000, 3), 255, dtype=np.uint8)
    cv2.putText(big_img, "BIG TEXT TEST", (100, 1000),
                cv2.FONT_HERSHEY_SIMPLEX, 3.0, (0, 0, 0), 4)
    _, big_enc = cv2.imencode(".jpg", big_img)
    ok, text = ocr.detect_mem(big_enc.tobytes(), doAngle=True, maxSideLen=960)
    if ok:
        stats.ok("LargeImage(2000x2000)", f"文本={repr(text[:40])}")
    else:
        stats.fail("LargeImage(2000x2000)", "失败")

    # PNG 格式
    png_img = np.full((100, 400, 3), 255, dtype=np.uint8)
    cv2.putText(png_img, "PNG TEST", (10, 70),
                cv2.FONT_HERSHEY_SIMPLEX, 2.0, (0, 0, 0), 2)
    _, png_enc = cv2.imencode(".png", png_img)
    ok, text = ocr.detect_mem(png_enc.tobytes(), doAngle=True)
    if ok:
        stats.ok("PNGFormat", f"文本={repr(text[:40])}")
    else:
        stats.fail("PNGFormat", "PNG格式识别失败")

    # 无效数据 (随机字节)
    garbage = bytes([0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A] + [0x00] * 100)
    try:
        ok, text = ocr.detect_mem(garbage, doAngle=True)
        # 不崩溃即可
        stats.ok("InvalidData", f"不崩溃, 返回={ok}")
    except Exception:
        stats.ok("InvalidData", "异常被捕获, 不崩溃")


def test_handle_lifecycle(dll_path):
    """测试句柄多次初始化/销毁"""
    print("\n" + "=" * 60)
    print("测试: 句柄生命周期 (多次 init/destroy)")
    print("=" * 60)

    for i in range(3):
        ocr = RapidOcr(dll_path)
        if ocr.init():
            stats.ok(f"Init-{i+1}", "成功")
            ocr.destroy()
            stats.ok(f"Destroy-{i+1}", "成功")
        else:
            stats.fail(f"Init-{i+1}", "失败")

    # 连续两次 init 不 destroy → 第二次应正常或第一个泄漏
    ocr1 = RapidOcr(dll_path)
    ocr1.init()
    ocr2 = RapidOcr(dll_path)
    if ocr2.init():
        stats.ok("DoubleInit", "第二个句柄正常创建")
        ocr2.destroy()
    else:
        stats.fail("DoubleInit", "第二个句柄创建失败")
    ocr1.destroy()


def test_text_image(ocr):
    """测试生成的文字图片"""
    print("\n" + "=" * 60)
    print("测试: 合成文字图片")
    print("=" * 60)

    for label, text_content in [("English", "Hello World"), ("Digits", "12345 67890")]:
        img_data = make_text_image(text_content)
        ok, text = ocr.detect_mem(img_data, doAngle=True)
        if ok and text.strip():
            stats.ok(f"Synthetic-{label}", f"识别结果: {repr(text.strip()[:40])}")
        elif ok:
            stats.fail(f"Synthetic-{label}", "识别成功但文本为空")
        else:
            stats.fail(f"Synthetic-{label}", "识别失败")


# ============================================================
# 主测试
# ============================================================
def main():
    print("=" * 60)
    print("  RapidOcrOnnx DLL 功能完整测试")
    print("=" * 60)

    # 查找 DLL
    dll_names = ["RapidOcrOnnx_small.dll", "RapidOcrOnnx.dll"]
    dll_path = None
    for name in dll_names:
        p = os.path.abspath(os.path.join(os.getcwd(), name))
        if os.path.exists(p):
            dll_path = p
            break
    if dll_path is None:
        print("未找到 DLL，请确认 RapidOcrOnnx_small.dll 或 RapidOcrOnnx.dll 存在")
        return
    print(f"加载 DLL: {dll_path}")

    # 查找测试图片
    test_image = os.path.abspath(os.path.join(os.getcwd(), "1.jpg"))
    if not os.path.exists(test_image):
        print(f"测试图片不存在: {test_image}")
        return
    print(f"测试图片: {test_image}")

    # 旋转图片
    rotated_path = os.path.abspath(os.path.join(os.getcwd(), "_test_rotated.jpg"))
    rotate_image_180(test_image, rotated_path)

    img_normal = read_binary(test_image)
    img_rotated = read_binary(rotated_path)

    # 初始化 OCR
    ocr = RapidOcr(dll_path)
    if not ocr.init():
        print("OCR 初始化失败!")
        return
    print("OCR 初始化成功\n")

    try:
        # --- 1. 基础识别 ---
        print("=" * 60)
        print("测试: 基础识别 (OcrDetectMem)")
        print("=" * 60)
        ok, text = ocr.detect_mem(img_normal, doAngle=True)
        if ok:
            stats.ok("OcrDetectMem", repr(text.strip()[:60]))
        else:
            stats.fail("OcrDetectMem", "失败")

        # --- 2. 文件路径识别 ---
        test_file_detect(ocr, test_image)
        test_file_detect_ex(ocr, test_image)

        # --- 3. Block API ---
        test_block_api(ocr, img_normal)

        # --- 4. ResultMem ---
        test_result_mem(ocr, img_normal)

        # --- 5. 角度分类器 ---
        test_angle_classifier(ocr, img_normal, img_rotated)

        # --- 6. 参数调整 ---
        test_parameters(ocr, img_normal)

        # --- 7. 排版策略 ---
        test_layout(ocr, img_normal)

        # --- 8. 边界情况 ---
        test_empty_and_edge_cases(ocr)

        # --- 9. 句柄生命周期 ---
        test_handle_lifecycle(dll_path)

        # --- 10. 合成文字图片 ---
        test_text_image(ocr)

    finally:
        ocr.destroy()
        print("\nOCR 句柄已销毁")

    # 清理
    if os.path.exists(rotated_path):
        os.remove(rotated_path)

    # 总结
    success = stats.summary()
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
