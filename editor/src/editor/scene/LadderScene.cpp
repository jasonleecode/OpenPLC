#include "LadderScene.h"

#include <QApplication>
#include <QPainter>
#include <QGraphicsSceneMouseEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QInputDialog>
#include <QtMath>

#include "../items/ContactItem.h"
#include "../items/CoilItem.h"
#include "../items/FunctionBlockItem.h"
#include "../items/BaseItem.h"
#include "../../utils/UndoStack.h"

LadderScene::LadderScene(QObject *parent)
    : QGraphicsScene(parent), m_mode(Mode_Select)
{
    setSceneRect(-20, RailTopY - 20,
                 RightRailX + 100,
                 RailBottomY + 60);

    m_backgroundColor = QApplication::palette().base().color();
    m_gridColor       = QColor("#F0F0F0");
    m_gridColorFine   = QColor("#E0E0E0");

    m_undoStack = new QUndoStack(this);
}

// ══════════════════════════════════════════════════════════════
// addFunctionBlock —— 从 Library 拖放创建功能块（支持 undo）
// ══════════════════════════════════════════════════════════════
void LadderScene::addFunctionBlock(const QString& typeName, const QPointF& scenePos)
{
    auto snapG = [](qreal v) { return qRound(v / GridSize) * (qreal)GridSize; };
    const QPointF snapPos(snapG(scenePos.x()), snapG(scenePos.y()));

    auto* fb = new FunctionBlockItem(
        typeName, QString("%1_%2").arg(typeName).arg(m_fbCount++));
    fb->setPos(snapPos);
    int lid = m_nextLocalId++;
    fb->setData(0, lid);
    m_items[lid] = fb;
    m_undoStack->push(new AddItemCmd(this, fb, QString("Drop %1").arg(typeName)));
}

// ══════════════════════════════════════════════════════════════
// setMode —— 切换编辑模式（含临时导线清理）
// ══════════════════════════════════════════════════════════════
void LadderScene::setMode(EditorMode mode)
{
    if (m_tempWire && mode != Mode_AddWire) {
        removeItem(m_tempWire);
        delete m_tempWire;
        m_tempWire = nullptr;
    }
    m_showPortSnap = false;
    m_mode = mode;
    emit modeChanged(mode);
}

// ══════════════════════════════════════════════════════════════
// drawBackground —— LD 专用：点阵 + 梯级水平母线 + 电源母线 + 梯级编号
// ══════════════════════════════════════════════════════════════
void LadderScene::drawBackground(QPainter *painter, const QRectF &rect)
{
    const QColor bg  = QApplication::palette().base().color();
    const bool dark  = bg.lightnessF() <= 0.5;
    painter->fillRect(rect, bg);

    // ── 1. 轻量点阵背景（间距 20px） ─────────────────────────
    {
        painter->setPen(QPen(dark ? QColor("#3A3A3A") : QColor("#CCCCCC"), 1.0));
        const int dot = 20;
        int fx = (int)rect.left()  - ((int)rect.left()  % dot);
        int fy = (int)rect.top()   - ((int)rect.top()   % dot);
        for (int x = fx; x <= (int)rect.right();  x += dot)
            for (int y = fy; y <= (int)rect.bottom(); y += dot)
                painter->drawPoint(x, y);
    }

    // ── 2. 梯级分隔线 + 编号 ─────────────────────────────────
    {
        int firstRung = qMax(0, (int)(rect.top()    / RungHeight));
        int lastRung  = qMin(RailBottomY / RungHeight + 1,
                             (int)(rect.bottom() / RungHeight) + 2);

        painter->setPen(QPen(QColor("#D8E4EE"), 1, Qt::SolidLine));
        for (int i = firstRung; i <= lastRung; ++i)
            painter->drawLine(QLineF(0, i * RungHeight, RightRailX + 60, i * RungHeight));

        QFont numFont("Courier New", 8);
        painter->setFont(numFont);
        painter->setPen(QColor("#AABBCC"));
        for (int i = firstRung; i < lastRung && i * RungHeight < RailBottomY; ++i) {
            painter->drawText(
                QRectF(2, i * RungHeight + 3, LeftRailX - 6, RungHeight - 6),
                Qt::AlignRight | Qt::AlignTop,
                QString("%1").arg(i + 1, 3, 10, QChar('0')));
        }
    }

    // ── 3. 梯级水平母线（每梯级中心横贯 L+ → N） ────────────
    {
        int firstRung = qMax(0, (int)(rect.top()    / RungHeight));
        int lastRung  = qMin(RailBottomY / RungHeight,
                             (int)(rect.bottom() / RungHeight) + 1);

        painter->setPen(QPen(QColor("#1A2E4A"), 1.5, Qt::SolidLine,
                             Qt::FlatCap, Qt::MiterJoin));
        for (int i = firstRung; i < lastRung; ++i) {
            qreal y = i * RungHeight + RungHeight / 2.0;
            painter->drawLine(QLineF(LeftRailX, y, RightRailX, y));
        }
    }

    // ── 4. 左电源母线 L+ ─────────────────────────────────────
    {
        qreal visTop    = qMax(rect.top(),    (qreal)RailTopY);
        qreal visBottom = qMin(rect.bottom(), (qreal)RailBottomY);
        if (visTop < visBottom) {
            painter->setPen(QPen(QColor("#1A2E4A"), 5, Qt::SolidLine, Qt::FlatCap));
            painter->drawLine(QLineF(LeftRailX, visTop, LeftRailX, visBottom));
        }
        if (rect.top() < RailTopY + 30) {
            QFont f; f.setPointSize(9); f.setBold(true);
            painter->setFont(f);
            painter->setPen(QColor("#1A2E4A"));
            painter->drawText(QRectF(LeftRailX - 22, RailTopY - 20, 44, 18),
                              Qt::AlignCenter, "L+");
        }
    }

    // ── 5. 右电源母线 N ──────────────────────────────────────
    {
        qreal visTop    = qMax(rect.top(),    (qreal)RailTopY);
        qreal visBottom = qMin(rect.bottom(), (qreal)RailBottomY);
        if (visTop < visBottom) {
            painter->setPen(QPen(QColor("#1A2E4A"), 5, Qt::SolidLine, Qt::FlatCap));
            painter->drawLine(QLineF(RightRailX, visTop, RightRailX, visBottom));
        }
        if (rect.top() < RailTopY + 30) {
            QFont f; f.setPointSize(9); f.setBold(true);
            painter->setFont(f);
            painter->setPen(QColor("#1A2E4A"));
            painter->drawText(QRectF(RightRailX - 22, RailTopY - 20, 44, 18),
                              Qt::AlignCenter, "N");
        }
    }
}

// ══════════════════════════════════════════════════════════════
// drawForeground —— 端口吸附指示器（绿色圆圈）
// ══════════════════════════════════════════════════════════════
void LadderScene::drawForeground(QPainter *painter, const QRectF &/*rect*/)
{
    if (!m_showPortSnap) return;
    painter->save();
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setPen(QPen(QColor("#00AA44"), 1.5));
    painter->setBrush(QColor(0, 170, 68, 60));
    painter->drawEllipse(m_portSnapPos, 9.0, 9.0);
    painter->restore();
}

// ══════════════════════════════════════════════════════════════
// snapToNearestPort —— 找离 pos 最近的元件端口
// ══════════════════════════════════════════════════════════════
QPointF LadderScene::snapToNearestPort(const QPointF& pos, qreal radius) const
{
    QPointF best = pos;
    qreal bestSq = radius * radius;

    auto check = [&](QPointF p) {
        qreal dx = p.x() - pos.x(), dy = p.y() - pos.y();
        qreal d = dx*dx + dy*dy;
        if (d < bestSq) { bestSq = d; best = p; }
    };

    for (QGraphicsItem* gi : items()) {
        if (auto* bi = dynamic_cast<BaseItem*>(gi)) {
            check(bi->leftPort());
            check(bi->rightPort());
        }
        if (auto* fb = dynamic_cast<FunctionBlockItem*>(gi)) {
            for (int i = 0; i < fb->inputCount();  ++i) check(fb->inputPortPos(i));
            for (int i = 0; i < fb->outputCount(); ++i) check(fb->outputPortPos(i));
        }
    }
    return best;
}

// ══════════════════════════════════════════════════════════════
// mousePressEvent —— 放置元件 / 画导线（含 undo）
// ══════════════════════════════════════════════════════════════
void LadderScene::mousePressEvent(QGraphicsSceneMouseEvent *event)
{
    if (event->button() != Qt::LeftButton || m_mode == Mode_Select) {
        QGraphicsScene::mousePressEvent(event);
        // 选择模式下：记录当前选中 items 的位置，供拖拽结束时对比
        if (m_mode == Mode_Select && event->button() == Qt::LeftButton) {
            m_dragStartPos.clear();
            for (QGraphicsItem* gi : selectedItems())
                m_dragStartPos[gi] = gi->pos();
        }
        return;
    }

    const QPointF raw = event->scenePos();
    auto snapG = [&](qreal v) { return qRound(v / GridSize) * (qreal)GridSize; };
    const QPointF snapPt(snapG(raw.x()), snapG(raw.y()));

    // ── 导线模式（端口优先吸附） ─────────────────────────────
    if (m_mode == Mode_AddWire) {
        QPointF snap = snapToNearestPort(raw, 20.0);
        if (snap == raw) snap = snapPt;

        if (!m_tempWire) {
            // 第一次点击：创建预览导线，不计入 undo
            m_tempWire = new WireItem(snap, snap);
            addItem(m_tempWire);
        } else {
            // 第二次点击：确认导线，推入 undo 命令
            m_tempWire->setEndPos(snap);
            m_undoStack->push(new AddItemCmd(this, m_tempWire, "Add Wire"));
            m_tempWire     = nullptr;
            m_showPortSnap = false;
            update();
        }
        return;
    }

    // ── 功能块（自由网格放置）────────────────────────────────
    if (m_mode == Mode_AddFuncBlock) {
        auto* fb = new FunctionBlockItem(
            "TON", QString("TON_%1").arg(m_fbCount++));
        fb->setPos(snapPt);
        int fbLid = m_nextLocalId++;
        fb->setData(0, fbLid);
        m_items[fbLid] = fb;
        m_undoStack->push(new AddItemCmd(this, fb, "Add Function Block"));
        return;
    }

    // ── 触点 / 线圈（网格放置）───────────────────────────────
    BaseItem* newItem = nullptr;
    switch (m_mode) {
    case Mode_AddContact_NO:
        newItem = new ContactItem(ContactItem::NormalOpen);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_NC:
        newItem = new ContactItem(ContactItem::NormalClosed);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_P:
        newItem = new ContactItem(ContactItem::PositiveTransition);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddContact_N:
        newItem = new ContactItem(ContactItem::NegativeTransition);
        static_cast<ContactItem*>(newItem)->setTagName(
            QString("X%1").arg(m_contactCount++));
        break;
    case Mode_AddCoil:
        newItem = new CoilItem(CoilItem::Output);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    case Mode_AddCoil_S:
        newItem = new CoilItem(CoilItem::SetCoil);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    case Mode_AddCoil_R:
        newItem = new CoilItem(CoilItem::ResetCoil);
        static_cast<CoilItem*>(newItem)->setTagName(
            QString("Y%1").arg(m_coilCount++));
        break;
    default:
        break;
    }

    if (newItem) {
        newItem->setPos(snapPt);
        int itemLid = m_nextLocalId++;
        newItem->setData(0, itemLid);
        m_items[itemLid] = newItem;
        m_undoStack->push(new AddItemCmd(this, newItem,
            QString("Add %1").arg(newItem->metaObject()->className())));
    }
}

// ══════════════════════════════════════════════════════════════
// mouseMoveEvent —— 更新导线预览 + 端口吸附指示器
// ══════════════════════════════════════════════════════════════
void LadderScene::mouseMoveEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseMoveEvent(event);

    if (m_mode == Mode_AddWire) {
        const QPointF raw  = event->scenePos();
        const QPointF snap = snapToNearestPort(raw, 20.0);

        bool hadSnap   = m_showPortSnap;
        m_showPortSnap = (snap != raw);
        m_portSnapPos  = snap;

        if (m_tempWire) {
            QPointF end = m_showPortSnap ? snap :
                QPointF(qRound(raw.x() / GridSize) * (qreal)GridSize,
                        qRound(raw.y() / GridSize) * (qreal)GridSize);
            m_tempWire->setEndPos(end);
        }

        if (hadSnap != m_showPortSnap || m_tempWire)
            update();

    } else if (m_showPortSnap) {
        m_showPortSnap = false;
        update();
    }
}

// ══════════════════════════════════════════════════════════════
// mouseReleaseEvent —— 检测拖拽移动，生成 MoveItemsCmd
// ══════════════════════════════════════════════════════════════
void LadderScene::mouseReleaseEvent(QGraphicsSceneMouseEvent *event)
{
    QGraphicsScene::mouseReleaseEvent(event);

    if (event->button() == Qt::LeftButton && m_mode == Mode_Select
        && !m_dragStartPos.isEmpty())
    {
        QList<MoveEntry> moves;
        for (auto it = m_dragStartPos.cbegin(); it != m_dragStartPos.cend(); ++it) {
            QGraphicsItem* gi = it.key();
            if (!items().contains(gi)) continue;
            const QPointF newPos = gi->pos();
            if (qAbs(newPos.x() - it.value().x()) > 0.5 ||
                qAbs(newPos.y() - it.value().y()) > 0.5)
            {
                moves << MoveEntry{gi, it.value(), newPos};
            }
        }
        if (!moves.isEmpty())
            m_undoStack->push(new MoveItemsCmd(moves,
                moves.size() == 1 ? "Move Item" : "Move Items"));
        m_dragStartPos.clear();
    }
}

// ══════════════════════════════════════════════════════════════
// keyPressEvent —— Delete 删除选中（含 undo）；Escape 返回 Select
// ══════════════════════════════════════════════════════════════
void LadderScene::keyPressEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Delete ||
        event->key() == Qt::Key_Backspace)
    {
        const auto sel = selectedItems();
        if (!sel.isEmpty())
            m_undoStack->push(new DeleteItemsCmd(this, sel,
                sel.size() == 1 ? "Delete Item" : "Delete Items"));
        if (m_tempWire && !items().contains(m_tempWire))
            m_tempWire = nullptr;
        event->accept();
    }
    else if (event->key() == Qt::Key_Escape) {
        if (m_tempWire) {
            removeItem(m_tempWire);
            delete m_tempWire;
            m_tempWire = nullptr;
        }
        m_showPortSnap = false;
        setMode(Mode_Select);
        event->accept();
    }
    else {
        QGraphicsScene::keyPressEvent(event);
    }
}

// ══════════════════════════════════════════════════════════════
// contextMenuEvent —— 右键菜单（含 undo）
// ══════════════════════════════════════════════════════════════
void LadderScene::contextMenuEvent(QGraphicsSceneContextMenuEvent *event)
{
    const QPointF scenePos = event->scenePos();
    auto snapG = [&](qreal v) { return qRound(v / GridSize) * (qreal)GridSize; };
    const QPointF snapPt(snapG(scenePos.x()), snapG(scenePos.y()));

    // 找点击位置的 BaseItem
    BaseItem* hitItem = nullptr;
    for (QGraphicsItem* gi : items(scenePos)) {
        hitItem = dynamic_cast<BaseItem*>(gi);
        if (hitItem) break;
    }

    QMenu menu;

    if (hitItem) {
        // ── 元件右键 ────────────────────────────────────────
        auto* editAct = menu.addAction(QIcon(":/images/edit.png"),   "Edit Name...");
        menu.addSeparator();
        auto* delAct  = menu.addAction(QIcon(":/images/Delete.png"), "Delete");

        QAction* chosen = menu.exec(event->screenPos());
        if (chosen == editAct)
            hitItem->editProperties();
        else if (chosen == delAct)
            m_undoStack->push(new DeleteItemsCmd(this, {hitItem}, "Delete Item"));

    } else {
        // ── 空白区域右键：快速添加元件 ──────────────────────
        auto* addNO  = menu.addAction(QIcon(":/images/add_contact.png"), "Add Contact (NO)");
        auto* addNC  = menu.addAction(QIcon(":/images/add_contact.png"), "Add Contact (NC)");
        menu.addSeparator();
        auto* addOut = menu.addAction(QIcon(":/images/add_coil.png"), "Add Coil (Output)");
        auto* addSet = menu.addAction(QIcon(":/images/add_coil.png"), "Add Set Coil (S)");
        auto* addRst = menu.addAction(QIcon(":/images/add_coil.png"), "Add Reset Coil (R)");
        menu.addSeparator();
        auto* addFb  = menu.addAction(QIcon(":/images/add_block.png"), "Add Function Block...");

        QAction* chosen = menu.exec(event->screenPos());

        BaseItem* newItem = nullptr;
        if (chosen == addNO) {
            auto* c = new ContactItem(ContactItem::NormalOpen);
            c->setTagName(QString("X%1").arg(m_contactCount++));
            newItem = c;
        } else if (chosen == addNC) {
            auto* c = new ContactItem(ContactItem::NormalClosed);
            c->setTagName(QString("X%1").arg(m_contactCount++));
            newItem = c;
        } else if (chosen == addOut) {
            auto* c = new CoilItem(CoilItem::Output);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addSet) {
            auto* c = new CoilItem(CoilItem::SetCoil);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addRst) {
            auto* c = new CoilItem(CoilItem::ResetCoil);
            c->setTagName(QString("Y%1").arg(m_coilCount++));
            newItem = c;
        } else if (chosen == addFb) {
            bool ok;
            const QString fbType = QInputDialog::getItem(
                nullptr, "Add Function Block", "Block type:",
                {"TON", "TOF", "CTU", "CTD", "CTUD",
                 "ADD", "SUB", "MUL", "DIV", "SEL", "MUX", "SR", "RS"},
                0, false, &ok);
            if (ok) {
                auto* fb = new FunctionBlockItem(
                    fbType, QString("%1_%2").arg(fbType).arg(m_fbCount++));
                fb->setPos(snapPt);
                int fbLid = m_nextLocalId++;
                fb->setData(0, fbLid);
                m_items[fbLid] = fb;
                m_undoStack->push(new AddItemCmd(this, fb, "Add Function Block"));
            }
            event->accept();
            return;
        }

        if (newItem) {
            newItem->setPos(snapPt);
            int itemLid = m_nextLocalId++;
            newItem->setData(0, itemLid);
            m_items[itemLid] = newItem;
            m_undoStack->push(new AddItemCmd(this, newItem,
                QString("Add %1").arg(newItem->metaObject()->className())));
        }
    }

    event->accept();
}
