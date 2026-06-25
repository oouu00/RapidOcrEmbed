#ifndef ICONS_H
#define ICONS_H

//
// 单色蓝色线条图标 (强调蓝 #5B9BD5)
// SVG 内嵌, 不依赖外部文件, 不增加运行时文件
// 全部为 24x24 viewBox, 细线 1.6px, 风格统一
//

#include <QString>
#include <QIcon>
#include <QPixmap>
#include <QPainter>
#include <QSvgRenderer>

namespace AppIcons {

// 统一图标颜色 (与界面强调蓝一致)
inline constexpr const char* COLOR = "#5B9BD5";

// 通用 SVG 包装: 自动着色 + viewBox
inline QString wrap(const char* body) {
    return QString(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\""
        " viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\""
        " stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>"
    ).arg(COLOR, body);
}

// 指定颜色的 SVG 包装 (用于开关按钮的选中/未选中状态)
inline QString wrapWithColor(const QString& body, const char* color) {
    return QString(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"24\" height=\"24\""
        " viewBox=\"0 0 24 24\" fill=\"none\" stroke=\"%1\""
        " stroke-width=\"1.6\" stroke-linecap=\"round\" stroke-linejoin=\"round\">%2</svg>"
    ).arg(color, body);
}

// 各图标 SVG 主体 (24x24)

// 截图: 虚线选框 + 角标
inline QString screenshot() {
    return wrap(
        "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"2\""
        " stroke-dasharray=\"3 2\"/>"
        "<path d=\"M3 8 V3 H8\"/>"
        "<path d=\"M21 16 V21 H16\"/>"
    );
}

// 选择: 文件夹
inline QString file() {
    return wrap(
        "<path d=\"M3 7 a2 2 0 0 1 2-2 h4 l2 2 h8 a2 2 0 0 1 2 2 v8 a2 2 0 0 1 -2 2 H5"
        " a2 2 0 0 1 -2 -2 Z\"/>"
    );
}

// 清空: 垃圾桶
inline QString trash() {
    return wrap(
        "<path d=\"M4 7 H20\"/>"
        "<path d=\"M9 7 V5 a1 1 0 0 1 1-1 h4 a1 1 0 0 1 1 1 V7\"/>"
        "<path d=\"M6 7 L7 20 a1 1 0 0 0 1 1 h8 a1 1 0 0 0 1-1 L18 7\"/>"
        "<path d=\"M10 11 V17\"/><path d=\"M14 11 V17\"/>"
    );
}

// 置顶: 图钉
inline QString pin() {
    return wrap(
        "<path d=\"M9 3 h6 v5 l3 4 H6 l3-4 Z\"/>"
        "<path d=\"M12 16 V21\"/>"
    );
}

// 矩形截图: 实心矩形
inline QString rect() {
    return wrap("<rect x=\"4\" y=\"4\" width=\"16\" height=\"16\" rx=\"1\"/>");
}

// 任意形状: 不规则路径
inline QString free() {
    return wrap(
        "<path d=\"M5 9 Q4 4 9 5 Q14 3 17 7 Q21 11 18 16 Q14 21 9 18 Q3 15 5 9 Z\"/>"
    );
}

// 控件截图: 窗口+光标
inline QString widget() {
    return wrap(
        "<rect x=\"3\" y=\"5\" width=\"18\" height=\"14\" rx=\"1\"/>"
        "<path d=\"M3 9 H21\"/>"
        "<path d=\"M14 13 l5 2 l-2 1 l-1 2 Z\" fill=\"%1\"/>"
    ).arg(COLOR);
}

// 窗口截图: 双层窗口
inline QString window() {
    return wrap(
        "<rect x=\"3\" y=\"5\" width=\"18\" height=\"14\" rx=\"1\"/>"
        "<path d=\"M3 9 H21\"/>"
        "<circle cx=\"6\" cy=\"7\" r=\"0.6\" fill=\"%1\"/>"
        "<circle cx=\"8.5\" cy=\"7\" r=\"0.6\" fill=\"%1\"/>"
    ).arg(COLOR);
}

// 全屏截图: 显示器+四角
inline QString fullscreen() {
    return wrap(
        "<rect x=\"3\" y=\"4\" width=\"18\" height=\"13\" rx=\"1\"/>"
        "<path d=\"M8 21 H16\"/><path d=\"M12 17 V21\"/>"
        "<path d=\"M3 8 V6\"/><path d=\"M21 8 V6\"/>"
    );
}

// 下拉箭头 (小三角, 用于 ▼ 按钮)
inline QString dropdown() {
    return QString(
        "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"12\" height=\"12\""
        " viewBox=\"0 0 12 12\" fill=\"%1\">"
        "<path d=\"M2 4 L10 4 L6 9 Z\"/></svg>"
    ).arg(COLOR);
}

// 最小化图标 (横线向下, 用于最小化到托盘)
inline QString minimize() {
    return wrap(
        "<path d=\"M5 11 H19\"/>"
        "<path d=\"M12 11 V17\"/>"
    );
}

// 表格图标 (网格, 用于表格模式开关)
inline QString tableBody() {
    return
        "<rect x=\"3\" y=\"3\" width=\"18\" height=\"18\" rx=\"1\"/>"
        "<path d=\"M3 9 H21\"/><path d=\"M3 15 H21\"/>"
        "<path d=\"M9 3 V21\"/><path d=\"M15 3 V21\"/>";
}

inline QString table() {
    return wrap(tableBody().toUtf8().constData());
}

// 网络图标 (地球+经线, 用于 HTTP 服务开关)
inline QString networkBody() {
    return
        "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
        "<path d=\"M3 12 H21\"/>"
        "<path d=\"M12 3 C7.5 6 7.5 18 12 21\"/>"
        "<path d=\"M12 3 C16.5 6 16.5 18 12 21\"/>";
}

inline QString network() {
    return wrap(networkBody().toUtf8().constData());
}

// 自动保存图标 (软盘/保存)
inline QString autoSaveBody() {
    return
        "<rect x=\"4\" y=\"3\" width=\"16\" height=\"18\" rx=\"1\"/>"
        "<path d=\"M8 3 V9 H16 V3\"/>"
        "<path d=\"M8 14 H16 V21 H8 Z\"/>"
        "<path d=\"M10 17 H14\"/>";
}

inline QString autoSave() {
    return wrap(autoSaveBody().toUtf8().constData());
}

// 自动复制图标 (剪贴板)
inline QString autoCopyBody() {
    return
        "<rect x=\"7\" y=\"3\" width=\"10\" height=\"18\" rx=\"2\"/>"
        "<path d=\"M4 7 H3 a1 1 0 0 0-1 1 V19 a1 1 0 0 0 1 1 H14 a1 1 0 0 0 1-1 V18\"/>";
}

inline QString autoCopy() {
    return wrap(autoCopyBody().toUtf8().constData());
}

// 坐标模式图标 (十字准星)
inline QString coordBody() {
    return
        "<circle cx=\"12\" cy=\"12\" r=\"8\"/>"
        "<path d=\"M12 4 V20\"/>"
        "<path d=\"M4 12 H20\"/>";
}

inline QString coord() {
    return wrap(coordBody().toUtf8().constData());
}

// 旋转图标 (循环箭头)
inline QString rotateBody() {
    return
        "<path d=\"M12 3a9 9 0 1 1-6.36-2.64\"/>"
        "<path d=\"M12 3V8l4-3-4-3\"/>";
}
inline QString rotate() { return wrap(rotateBody().toUtf8().constData()); }

// 齿轮图标 (高级设置)
inline QString gearBody() {
    return
        "<path d=\"M12 15a3 3 0 1 0 0-6 3 3 0 0 0 0 6 Z\"/>"
        "<path d=\"M19.4 15a1.65 1.65 0 0 0 .33 1.82l.06.06a2 2 0 1 1-2.83 2.83l-.06-.06a1.65 1.65 0 0 0-1.82-.33 1.65 1.65 0 0 0-1 1.51V21a2 2 0 0 1-4 0v-.09A1.65 1.65 0 0 0 9 19.4a1.65 1.65 0 0 0-1.82.33l-.06.06a2 2 0 1 1-2.83-2.83l.06-.06A1.65 1.65 0 0 0 4.68 15a1.65 1.65 0 0 0-1.51-1H3a2 2 0 0 1 0-4h.09A1.65 1.65 0 0 0 4.6 9a1.65 1.65 0 0 0-.33-1.82l-.06-.06a2 2 0 1 1 2.83-2.83l.06.06A1.65 1.65 0 0 0 9 4.68a1.65 1.65 0 0 0 1-1.51V3a2 2 0 0 1 4 0v.09a1.65 1.65 0 0 0 1 1.51 1.65 1.65 0 0 0 1.82-.33l.06-.06a2 2 0 1 1 2.83 2.83l-.06.06A1.65 1.65 0 0 0 19.4 9a1.65 1.65 0 0 0 1.51 1H21a2 2 0 0 1 0 4h-.09a1.65 1.65 0 0 0-1.51 1 Z\"/>";
}
inline QString gear() { return wrap(gearBody().toUtf8().constData()); }

// 问号图标 (帮助/说明)
inline QString helpBody() {
    return
        "<circle cx=\"12\" cy=\"12\" r=\"9\"/>"
        "<path d=\"M9.09 9a3 3 0 0 1 5.83 1c0 2-3 3-3 3\"/>"
        "<path d=\"M12 17h.01\"/>";
}
inline QString help() { return wrap(helpBody().toUtf8().constData()); }

// 从 SVG 字符串生成指定尺寸的 QIcon (通过 QSvgRenderer 渲染)
inline QIcon fromSvg(const QString& svg, int size = 20) {
    QSvgRenderer renderer(svg.toUtf8());
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.setRenderHint(QPainter::Antialiasing, true);
    painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    renderer.render(&painter);
    painter.end();
    return QIcon(pm);
}

} // namespace AppIcons

#endif // ICONS_H
