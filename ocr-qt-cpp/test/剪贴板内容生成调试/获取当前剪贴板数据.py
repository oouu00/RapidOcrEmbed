import win32clipboard
import sys
import os
from datetime import datetime

def get_clipboard_excel():
    """获取Excel复制的完整内容（支持多种格式）"""
    content = None
    format_type = None
    
    try:
        win32clipboard.OpenClipboard()
        
        # 尝试获取多种格式
        formats_to_try = [
            (win32clipboard.RegisterClipboardFormat("HTML Format"), "HTML"),
            (win32clipboard.CF_UNICODETEXT, "Unicode Text"),
            (win32clipboard.CF_TEXT, "Text"),
            (win32clipboard.CF_OEMTEXT, "OEM Text"),
        ]
        
        for fmt, fmt_name in formats_to_try:
            try:
                data = win32clipboard.GetClipboardData(fmt)
                if data:
                    # 确保转换为字符串
                    if isinstance(data, bytes):
                        content = data.decode('utf-8', errors='ignore')
                    else:
                        content = str(data)
                    format_type = fmt_name
                    print(f"成功获取格式: {fmt_name}")
                    break
            except:
                continue
        
        win32clipboard.CloseClipboard()
        
        return content, format_type
        
    except Exception as e:
        print(f"剪贴板错误: {e}")
        try:
            win32clipboard.CloseClipboard()
        except:
            pass
        return None, None

def save_to_file(content, filename):
    """保存内容到文件"""
    if not content:
        print("没有内容可保存")
        return False
    
    try:
        with open(filename, 'w', encoding='utf-8', errors='ignore') as f:
            f.write(content)
        print(f"已保存到: {os.path.abspath(filename)}")
        print(f"内容长度: {len(content)} 字符")
        return True
    except Exception as e:
        print(f"保存失败: {e}")
        return False

if __name__ == "__main__":
    print("正在读取剪贴板...")
    content, fmt = get_clipboard_excel()
    
    if content:
        # 生成带时间戳的文件名
        timestamp = datetime.now().strftime("%Y%m%d_%H%M%S")
        output_file = f"clipboard_{timestamp}.txt"
        save_to_file(content, output_file)
    else:
        print("无法获取剪贴板内容，可能需要以管理员权限运行")