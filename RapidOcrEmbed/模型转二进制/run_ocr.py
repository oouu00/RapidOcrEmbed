import numpy as np
import onnxruntime as ort
import cv2
import yaml

import os

# 路径配置（默认基于脚本所在目录，可按需修改）
# Directory layout (relative to this script):
#   模型转二进制/
#     run_ocr.py
#     1.jpg
#     models/
#       PP-OCRv6_small_det_onnx/PP-OCRv6_small_det_onnx.onnx
#       PP-OCRv6_small_rec_onnx/PP-OCRv6_small_rec_onnx.onnx
#       PP-OCRv6_small_rec_onnx/PP-OCRv6_small_rec_onnx.yml
_BASE = os.path.dirname(os.path.abspath(__file__))
_MODELS = os.path.join(_BASE, "models")

IMAGE_PATH = os.environ.get("OCR_IMAGE_PATH") or os.path.join(_BASE, "1.jpg")
DET_MODEL_PATH = os.environ.get("OCR_DET_MODEL") or os.path.join(
    _MODELS, "PP-OCRv6_small_det_onnx", "PP-OCRv6_small_det_onnx.onnx")
REC_MODEL_PATH = os.environ.get("OCR_REC_MODEL") or os.path.join(
    _MODELS, "PP-OCRv6_small_rec_onnx", "PP-OCRv6_small_rec_onnx.onnx")
REC_CONFIG_PATH = os.environ.get("OCR_REC_CONFIG") or os.path.join(
    _MODELS, "PP-OCRv6_small_rec_onnx", "PP-OCRv6_small_rec_onnx.yml")

# 从 yml 文件加载字符字典
with open(REC_CONFIG_PATH, 'r', encoding='utf-8') as f:
    rec_config = yaml.safe_load(f)
character_dict = rec_config['PostProcess']['character_dict']
# 在开头插入 blank 符号
character_dict = ['blank'] + character_dict

# 创建 ONNX Runtime session
det_session = ort.InferenceSession(DET_MODEL_PATH, providers=['CPUExecutionProvider'])
rec_session = ort.InferenceSession(REC_MODEL_PATH, providers=['CPUExecutionProvider'])


def preprocess_det(image, target_size=736):
    """检测模型预处理"""
    h, w = image.shape[:2]
    scale = target_size / max(h, w)
    new_h = (int(h * scale) // 32) * 32
    new_w = (int(w * scale) // 32) * 32
    
    resized = cv2.resize(image, (new_w, new_h))
    
    # ImageNet 标准化
    mean = np.array([0.485 * 255, 0.456 * 255, 0.406 * 255], dtype=np.float32)
    norm = np.array([1.0 / 0.229 / 255.0, 1.0 / 0.224 / 255.0, 1.0 / 0.225 / 255.0], dtype=np.float32)
    resized = resized.astype(np.float32)
    resized = (resized - mean) * norm
    
    resized = resized.transpose(2, 0, 1)
    resized = np.expand_dims(resized, axis=0).astype(np.float32)
    
    return resized, h, w


def postprocess_det(output, orig_h, orig_w, thresh=0.3):
    """检测后处理"""
    output = output[0][0]
    det_h, det_w = output.shape
    
    scale_x = orig_w / det_w
    scale_y = orig_h / det_h
    
    bitmap = output > thresh
    contours, _ = cv2.findContours((bitmap * 255).astype(np.uint8), cv2.RETR_EXTERNAL, cv2.CHAIN_APPROX_SIMPLE)
    
    boxes = []
    for contour in contours:
        if cv2.contourArea(contour) < 100:
            continue
        rect = cv2.minAreaRect(contour)
        box = cv2.boxPoints(rect).astype(np.float32)
        box[:, 0] *= scale_x
        box[:, 1] *= scale_y
        
        # 排序: 左上, 右上, 右下, 左下
        x_sorted = box[np.argsort(box[:, 0])]
        left = x_sorted[:2][np.argsort(x_sorted[:2, 1])]
        right = x_sorted[2:][np.argsort(x_sorted[2:, 1])]
        box = np.array([left[0], right[0], right[1], left[1]])
        
        boxes.append(box)
    
    return boxes


def preprocess_rec(image):
    """识别模型预处理"""
    h, w = image.shape[:2]
    target_h = 48
    scale = target_h / h
    new_w = min(int(w * scale), 3200)
    
    resized = cv2.resize(image, (new_w, target_h))
    
    # 归一化到 [-1, 1]
    resized = resized.astype(np.float32)
    resized = (resized - 127.5) / 127.5
    
    resized = resized.transpose(2, 0, 1)
    resized = np.expand_dims(resized, axis=0).astype(np.float32)
    
    return resized


def get_rotate_crop_image(image, box):
    """从原图中裁剪文本区域"""
    pts = box.astype(np.float32)
    
    # 计算宽高
    width = int(max(np.linalg.norm(pts[1] - pts[0]), np.linalg.norm(pts[2] - pts[3])))
    height = int(max(np.linalg.norm(pts[0] - pts[3]), np.linalg.norm(pts[1] - pts[2])))
    
    if width < 1 or height < 1:
        return None
    
    dst_pts = np.array([[0, 0], [width-1, 0], [width-1, height-1], [0, height-1]], dtype=np.float32)
    M = cv2.getPerspectiveTransform(pts, dst_pts)
    cropped = cv2.warpPerspective(image, M, (width, height), cv2.BORDER_REPLICATE)
    
    # 如果高度大于宽度的1.5倍，旋转90度
    if height >= width * 1.5:
        cropped = cv2.transpose(cropped)
        cropped = cv2.flip(cropped, 0)
    
    return cropped


def ctc_decode(output):
    """CTC 解码"""
    output = output[0]  # (seq_len, num_classes)
    text = ""
    prev_idx = -1
    for step in range(output.shape[0]):
        pred = output[step]
        char_idx = np.argmax(pred)
        if char_idx > 0 and char_idx != prev_idx:  # 跳过 blank 和连续相同字符
            if char_idx < len(character_dict):
                text += character_dict[char_idx]
        prev_idx = char_idx
    return text


def main():
    image = cv2.imread(IMAGE_PATH)
    if image is None:
        print(f"无法读取图片: {IMAGE_PATH}")
        return
    
    print(f"图片尺寸: {image.shape}")
    
    # 检测
    input_data, orig_h, orig_w = preprocess_det(image)
    det_output = det_session.run(None, {det_session.get_inputs()[0].name: input_data})[0]
    boxes = postprocess_det(det_output, orig_h, orig_w)
    print(f"检测到 {len(boxes)} 个文本区域")
    
    # 识别
    results = []
    for i, box in enumerate(boxes):
        cropped = get_rotate_crop_image(image, box)
        if cropped is None:
            continue
        
        input_data = preprocess_rec(cropped)
        rec_output = rec_session.run(None, {rec_session.get_inputs()[0].name: input_data})[0]
        text = ctc_decode(rec_output)
        
        if text:
            results.append((box, text))
    
    # 输出结果
    print("\n=== 识别结果 ===")
    for i, (box, text) in enumerate(results):
        print(f"{i+1}: {text}")
    
    # 可视化结果
    vis_image = image.copy()
    for box, text in results:
        pts = box.astype(np.int32)
        cv2.polylines(vis_image, [pts], True, (0, 255, 0), 2)
        # 在文本区域上方显示识别结果
        cv2.putText(vis_image, text, (int(pts[0][0]), int(pts[0][1]) - 10), 
                    cv2.FONT_HERSHEY_SIMPLEX, 0.8, (0, 0, 255), 2)
    
    cv2.imwrite("result.jpg", vis_image)
    print("\n结果已保存到 result.jpg")


if __name__ == "__main__":
    main()