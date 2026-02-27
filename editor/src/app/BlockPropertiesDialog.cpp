#include "BlockPropertiesDialog.h"

#include <QPainter>
#include <QPen>
#include <QBrush>
#include <QFont>
#include <QPolygonF>
#include <QDialog>
#include <QHBoxLayout>
#include <QVBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QFrame>
#include <QPushButton>
#include <QSizePolicy>
#include <QtMath>

// ═══════════════════════════════════════════════════════════════
// BlockPreviewWidget
// ═══════════════════════════════════════════════════════════════

static const int kRowH    = 22;   // 每个端口行高（像素）
static const int kHdrH    = 28;   // 类型名行高
static const int kInstH   = 16;   // 实例名行高（当有实例名时追加）
static const int kArrW    = 18;   // 箭头宽度（盒子外侧）
static const int kHPad    = 14;   // 盒子左右留边
static const int kVPad    = 18;   // 上方标题留空

BlockPreviewWidget::BlockPreviewWidget(QWidget* parent)
    : QWidget(parent)
{
    setSizePolicy(QSizePolicy::Fixed, QSizePolicy::Fixed);
    setMinimumSize(sizeHint());
}

void BlockPreviewWidget::setBlock(const QString& typeName,
                                   const QStringList& inNames,
                                   const QStringList& inTypes,
                                   const QStringList& outNames,
                                   const QStringList& outTypes,
                                   const QString& instanceName)
{
    m_name         = typeName;
    m_instanceName = instanceName;
    m_inNames      = inNames;
    m_inTypes      = inTypes;
    m_outNames     = outNames;
    m_outTypes     = outTypes;
    setFixedSize(sizeHint());
    update();
}

QSize BlockPreviewWidget::sizeHint() const
{
    const int nRows  = qMax(qMax(m_inNames.size(), m_outNames.size()), 1);
    const int hdrH   = kHdrH + (m_instanceName.isEmpty() ? 0 : kInstH);
    const int boxH   = hdrH + nRows * kRowH + 8;
    const int boxW   = 180;  // 固定盒子宽度
    const int w      = kHPad + kArrW + boxW + kArrW + kHPad;
    const int h      = kVPad + boxH + 16;
    return { w, h };
}

void BlockPreviewWidget::paintEvent(QPaintEvent*)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);

    const int nRows = qMax(qMax(m_inNames.size(), m_outNames.size()), 1);
    const int hdrH  = kHdrH + (m_instanceName.isEmpty() ? 0 : kInstH);
    const int boxH  = hdrH + nRows * kRowH + 8;
    const int boxW  = 180;
    const int boxX  = kHPad + kArrW;
    const int boxY  = kVPad;
    const int halfW = boxW / 2;

    // ── 背景 ────────────────────────────────────────────────
    p.fillRect(rect(), QColor("#FAFAFA"));

    // ── 外框 ────────────────────────────────────────────────
    p.setPen(QPen(QColor("#1A2E4A"), 1.5));
    p.setBrush(Qt::white);
    p.drawRect(boxX, boxY, boxW, boxH);

    // ── 标题区背景色（蓝色条，与 FunctionBlockItem 一致）────
    p.setBrush(QColor("#DDE8F5"));
    p.setPen(Qt::NoPen);
    p.drawRect(boxX + 1, boxY + 1, boxW - 2, hdrH - 2);

    // ── 中间竖分隔线 ────────────────────────────────────────
    p.setPen(QPen(QColor("#1A2E4A"), 1));
    p.drawLine(boxX + halfW, boxY + hdrH,
               boxX + halfW, boxY + boxH);

    // ── 顶部横分隔线 ────────────────────────────────────────
    p.drawLine(boxX, boxY + hdrH, boxX + boxW, boxY + hdrH);

    // ── 块类型名（加粗，居中于类型名行）─────────────────────
    QFont nameFont("Arial", 10, QFont::Bold);
    p.setFont(nameFont);
    p.setPen(QColor("#1A2E4A"));
    p.drawText(QRectF(boxX, boxY, boxW, kHdrH),
               Qt::AlignCenter, m_name);

    // ── 实例名（斜体小字，在类型名下方）─────────────────────
    if (!m_instanceName.isEmpty()) {
        QFont instFont("Arial", 8);
        instFont.setItalic(true);
        p.setFont(instFont);
        p.setPen(QColor("#555555"));
        p.drawText(QRectF(boxX + 2, boxY + kHdrH, boxW - 4, kInstH),
                   Qt::AlignCenter, m_instanceName);
    }

    // ── 端口字体 ─────────────────────────────────────────────
    QFont portFont("Courier New", 8);
    portFont.setBold(false);
    p.setFont(portFont);

    // ── 输入端口（左侧）──────────────────────────────────────
    for (int i = 0; i < m_inNames.size(); ++i) {
        const int cy = boxY + hdrH + i * kRowH + kRowH / 2;

        // 水平箭头线（从盒子左边向左延伸）
        p.setPen(QPen(QColor("#1A2E4A"), 1.2));
        p.drawLine(boxX - kArrW, cy, boxX, cy);

        // 箭头头（→，指向右方进入盒子）
        const int ax = boxX;
        QPolygonF arr;
        arr << QPointF(ax, cy)
            << QPointF(ax - 7, cy - 4)
            << QPointF(ax - 7, cy + 4);
        p.setBrush(QColor("#1A2E4A"));
        p.drawPolygon(arr);

        // 端口名（盒子内左半侧，左对齐）
        p.setPen(QColor("#222"));
        p.drawText(QRectF(boxX + 5, boxY + hdrH + i * kRowH + 1,
                          halfW - 10, kRowH),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   m_inNames[i]);
    }

    // ── 输出端口（右侧）──────────────────────────────────────
    for (int i = 0; i < m_outNames.size(); ++i) {
        const int cy = boxY + hdrH + i * kRowH + kRowH / 2;

        // 水平箭头线（从盒子右边向右延伸）
        p.setPen(QPen(QColor("#1A2E4A"), 1.2));
        p.drawLine(boxX + boxW, cy, boxX + boxW + kArrW, cy);

        // 箭头头（→，指向右方出盒子）
        const int ax = boxX + boxW + kArrW;
        QPolygonF arr;
        arr << QPointF(ax, cy)
            << QPointF(ax - 7, cy - 4)
            << QPointF(ax - 7, cy + 4);
        p.setBrush(QColor("#1A2E4A"));
        p.drawPolygon(arr);

        // 端口名（盒子内右半侧，右对齐）
        p.setPen(QColor("#222"));
        p.drawText(QRectF(boxX + halfW + 5, boxY + hdrH + i * kRowH + 1,
                          halfW - 10, kRowH),
                   Qt::AlignRight | Qt::AlignVCenter,
                   m_outNames[i]);
    }
}

// ═══════════════════════════════════════════════════════════════
// BlockPropertiesDialog
// ═══════════════════════════════════════════════════════════════

BlockPropertiesDialog::BlockPropertiesDialog(
    const QString& name,
    const QString& kind,
    const QString& comment,
    const QStringList& inNames,  const QStringList& inTypes,
    const QStringList& outNames, const QStringList& outTypes,
    QWidget* parent,
    const QString& instanceName)
    : QDialog(parent)
{
    const bool editable = !instanceName.isNull();

    setWindowTitle("Block Properties");
    setWindowFlags(windowFlags() & ~Qt::WindowContextHelpButtonHint);
    setModal(true);

    // ── 顶层水平布局：左侧信息 + 右侧 Preview ──────────────
    auto* rootLay = new QVBoxLayout(this);
    rootLay->setContentsMargins(12, 12, 12, 8);

    auto* bodyLay = new QHBoxLayout();
    bodyLay->setSpacing(16);

    // ── 左侧：信息面板 ────────────────────────────────────
    auto* infoWidget = new QWidget();
    infoWidget->setFixedWidth(210);
    auto* infoLay = new QVBoxLayout(infoWidget);
    infoLay->setContentsMargins(0, 0, 0, 0);
    infoLay->setSpacing(6);

    // ── 实例名（可编辑，仅当 editable=true 时显示）────────────
    if (editable) {
        auto* nameLay = new QHBoxLayout();
        nameLay->setSpacing(6);
        nameLay->addWidget(new QLabel("Name:"));
        m_nameEdit = new QLineEdit(instanceName);
        m_nameEdit->setPlaceholderText("Instance name…");
        nameLay->addWidget(m_nameEdit);
        infoLay->addLayout(nameLay);

        auto* nameSep = new QFrame();
        nameSep->setFrameShape(QFrame::HLine);
        nameSep->setFrameShadow(QFrame::Sunken);
        infoLay->addWidget(nameSep);
    }

    // 类型名
    auto* nameLabel = new QLabel(
        QString("<b style='font-size:13px;'>%1</b>").arg(name.toHtmlEscaped()));
    nameLabel->setWordWrap(false);
    infoLay->addWidget(nameLabel);

    // 种类
    const QString kindStr = (kind == "functionBlock") ? "Function Block" : "Function";
    auto* kindLabel = new QLabel(QString("Kind: <i>%1</i>").arg(kindStr));
    infoLay->addWidget(kindLabel);

    // 分隔线
    auto* sep1 = new QFrame();
    sep1->setFrameShape(QFrame::HLine);
    sep1->setFrameShadow(QFrame::Sunken);
    infoLay->addWidget(sep1);

    // 描述
    if (!comment.isEmpty()) {
        auto* descTitle = new QLabel("<b>Description:</b>");
        infoLay->addWidget(descTitle);
        auto* descLabel = new QLabel(comment);
        descLabel->setWordWrap(true);
        descLabel->setStyleSheet("color:#444;");
        infoLay->addWidget(descLabel);
    }

    // 输入端口列表
    if (!inNames.isEmpty()) {
        auto* sep2 = new QFrame();
        sep2->setFrameShape(QFrame::HLine);
        sep2->setFrameShadow(QFrame::Sunken);
        infoLay->addWidget(sep2);

        infoLay->addWidget(new QLabel("<b>Inputs:</b>"));
        for (int i = 0; i < inNames.size(); ++i) {
            const QString t = (i < inTypes.size()) ? inTypes[i] : "";
            auto* pl = new QLabel(
                QString("  <tt>%1</tt>: <i>%2</i>")
                    .arg(inNames[i].toHtmlEscaped())
                    .arg(t.toHtmlEscaped()));
            infoLay->addWidget(pl);
        }
    }

    // 输出端口列表
    if (!outNames.isEmpty()) {
        auto* sep3 = new QFrame();
        sep3->setFrameShape(QFrame::HLine);
        sep3->setFrameShadow(QFrame::Sunken);
        infoLay->addWidget(sep3);

        infoLay->addWidget(new QLabel("<b>Outputs:</b>"));
        for (int i = 0; i < outNames.size(); ++i) {
            const QString t = (i < outTypes.size()) ? outTypes[i] : "";
            auto* pl = new QLabel(
                QString("  <tt>%1</tt>: <i>%2</i>")
                    .arg(outNames[i].toHtmlEscaped())
                    .arg(t.toHtmlEscaped()));
            infoLay->addWidget(pl);
        }
    }

    infoLay->addStretch();

    // ── 右侧：Preview ──────────────────────────────────────
    auto* previewGroup = new QVBoxLayout();
    previewGroup->setSpacing(4);
    previewGroup->addWidget(new QLabel("<b>Preview:</b>"));

    auto* preview = new BlockPreviewWidget();
    preview->setBlock(name, inNames, inTypes, outNames, outTypes, instanceName);
    preview->setStyleSheet(
        "BlockPreviewWidget { background:#FAFAFA; border:1px solid #CCCCCC; }");
    previewGroup->addWidget(preview);
    previewGroup->addStretch();

    bodyLay->addWidget(infoWidget);
    bodyLay->addLayout(previewGroup);
    rootLay->addLayout(bodyLay);

    // ── 底部按钮 ────────────────────────────────────────────
    auto* btnLay = new QHBoxLayout();
    btnLay->addStretch();
    if (editable) {
        auto* cancelBtn = new QPushButton("Cancel");
        cancelBtn->setFixedWidth(80);
        connect(cancelBtn, &QPushButton::clicked, this, &QDialog::reject);
        btnLay->addWidget(cancelBtn);

        auto* okBtn = new QPushButton("OK");
        okBtn->setFixedWidth(80);
        okBtn->setDefault(true);
        connect(okBtn, &QPushButton::clicked, this, &QDialog::accept);
        btnLay->addWidget(okBtn);
    } else {
        auto* closeBtn = new QPushButton("Close");
        closeBtn->setFixedWidth(80);
        closeBtn->setDefault(true);
        connect(closeBtn, &QPushButton::clicked, this, &QDialog::accept);
        btnLay->addWidget(closeBtn);
    }
    rootLay->addLayout(btnLay);

    adjustSize();
    setFixedSize(sizeHint());
}

QString BlockPropertiesDialog::getInstanceName() const
{
    return m_nameEdit ? m_nameEdit->text() : QString();
}
