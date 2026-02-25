#pragma once
#include <QProxyStyle>
#include <QPainter>
#include <QStyleOption>
#include <QPolygonF>

// 自定义树控件分支绘制样式：虚线层级连线 + 填充三角折叠箭头
// 使用 QProxyStyle 拦截 PE_IndicatorBranch，完全不依赖图片文件
class TreeBranchStyle : public QProxyStyle
{
public:
    // 必须用无参构造：QProxyStyle(QStyle*) 会接管传入对象的所有权，
    // 若传入 widget->style()（应用级平台样式），关闭时会 double-free。
    // 无参构造内部引用应用样式但不接管所有权，不会崩溃。
    TreeBranchStyle() : QProxyStyle() {}

    void drawPrimitive(PrimitiveElement pe,
                       const QStyleOption* opt,
                       QPainter* p,
                       const QWidget* w) const override
    {
        if (pe != QStyle::PE_IndicatorBranch) {
            QProxyStyle::drawPrimitive(pe, opt, p, w);
            return;
        }

        const QRect  r  = opt->rect;
        const int    cx = r.left() + r.width()  / 2;
        const int    cy = r.top()  + r.height() / 2;
        const QColor lineColor(0x70, 0x70, 0x70);
        const QColor arrowColor(0x44, 0x44, 0x44);

        // ── 虚线连线 ──────────────────────────────────────────────
        p->save();
        p->setRenderHint(QPainter::Antialiasing, false);
        QPen dashPen(lineColor, 1, Qt::CustomDashLine);
        dashPen.setDashPattern(QList<qreal>{2.0, 2.0});
        p->setPen(dashPen);

        if (opt->state & QStyle::State_Item) {
            // 横线：从中心到右侧（连向当前项）
            p->drawLine(cx, cy, r.right(), cy);
            // 竖线：从顶到中心
            p->drawLine(cx, r.top(), cx, cy);
        }
        if (opt->state & QStyle::State_Sibling) {
            // 竖线：向下延伸到兄弟项（有更多同级节点时）
            int from = (opt->state & QStyle::State_Item) ? cy : r.top();
            p->drawLine(cx, from, cx, r.bottom());
        }

        // ── 折叠 / 展开三角箭头 ──────────────────────────────────
        if (opt->state & QStyle::State_Children) {
            p->setRenderHint(QPainter::Antialiasing);
            p->setBrush(arrowColor);
            p->setPen(Qt::NoPen);
            QPolygonF tri;
            if (opt->state & QStyle::State_Open) {
                // 下向三角（已展开）
                tri << QPointF(cx - 4, cy - 2)
                    << QPointF(cx + 4, cy - 2)
                    << QPointF(cx,     cy + 3);
            } else {
                // 右向三角（已折叠）
                tri << QPointF(cx - 3, cy - 4)
                    << QPointF(cx + 3, cy)
                    << QPointF(cx - 3, cy + 4);
            }
            p->drawPolygon(tri);
        }

        p->restore();
    }
};
