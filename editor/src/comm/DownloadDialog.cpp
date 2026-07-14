#include "DownloadDialog.h"
#include "IPlcTransport.h"
#include "SerialTransport.h"
#include "TcpTransport.h"
#include "PlcProtocol.h"

#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QTabWidget>
#include <QComboBox>
#include <QSpinBox>
#include <QLineEdit>
#include <QProgressBar>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QLabel>
#include <QFileDialog>
#include <QFile>
#include <QMessageBox>
#include <QDateTime>
#include <QFont>
#include <QFileInfo>

DownloadDialog::DownloadDialog(QWidget* parent)
    : QDialog(parent)
{
    setWindowTitle("Download Program to PLC");
    setMinimumSize(580, 520);
    setupUi();
}

void DownloadDialog::setBinaryPath(const QString& path)
{
    m_binPathEdit->setText(path);
}

// ─────────────────────────────────────────────────────────────────────────────
// UI 构建
// ─────────────────────────────────────────────────────────────────────────────
void DownloadDialog::setupUi()
{
    auto* root = new QVBoxLayout(this);
    root->setSpacing(8);
    root->setContentsMargins(12, 12, 12, 12);

    // ── 传输方式 Tab ─────────────────────────────────────────
    m_transportTabs = new QTabWidget;

    // ---- Serial 标签页 ----
    auto* serialWidget = new QWidget;
    auto* serialForm   = new QFormLayout(serialWidget);
    serialForm->setSpacing(6);

    auto* portRow = new QHBoxLayout;
    m_portCombo = new QComboBox;
    m_portCombo->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    m_btnRefresh = new QPushButton("Refresh");
    m_btnRefresh->setFixedWidth(70);
    portRow->addWidget(m_portCombo);
    portRow->addWidget(m_btnRefresh);
    serialForm->addRow("Port:", portRow);

    m_baudCombo = new QComboBox;
    for (int baud : {9600, 19200, 38400, 57600, 115200, 230400, 460800, 921600})
        m_baudCombo->addItem(QString::number(baud), baud);
    m_baudCombo->setCurrentText("115200");
    serialForm->addRow("Baud rate:", m_baudCombo);

    m_transportTabs->addTab(serialWidget, "Serial");

    // ---- Ethernet 标签页 ----
    auto* tcpWidget = new QWidget;
    auto* tcpLayout = new QVBoxLayout(tcpWidget);
    auto* tcpForm   = new QFormLayout;
    tcpForm->setSpacing(6);

    m_hostEdit = new QLineEdit("192.168.1.100");
    tcpForm->addRow("Host:", m_hostEdit);

    m_tcpPortSpin = new QSpinBox;
    m_tcpPortSpin->setRange(1, 65535);
    m_tcpPortSpin->setValue(6699);
    tcpForm->addRow("Port:", m_tcpPortSpin);

    auto* tcpNote = new QLabel("<i>Uses the same TiZi download protocol over TCP.<br>"
                               "The PLC runtime must expose the matching TCP service.</i>");
    tcpNote->setWordWrap(true);
    tcpNote->setStyleSheet("color: gray;");

    tcpLayout->addLayout(tcpForm);
    tcpLayout->addWidget(tcpNote);
    tcpLayout->addStretch();

    m_transportTabs->addTab(tcpWidget, "Ethernet");

    root->addWidget(m_transportTabs);

    // ── Binary 文件 ───────────────────────────────────────────
    auto* fileGroup  = new QGroupBox("Binary File");
    auto* fileLayout = new QVBoxLayout(fileGroup);
    fileLayout->setSpacing(4);

    auto* fileRow = new QHBoxLayout;
    m_binPathEdit = new QLineEdit;
    m_binPathEdit->setPlaceholderText("Path to user_logic.bin ...");
    auto* btnBrowse = new QPushButton("Browse...");
    btnBrowse->setFixedWidth(80);
    fileRow->addWidget(m_binPathEdit);
    fileRow->addWidget(btnBrowse);

    m_flashAddrLbl = new QLabel(
        QString("Flash base: <b>0x%1</b>  (UserLogic B partition)")
        .arg(PlcProtocol::USER_FLASH_BASE, 8, 16, QChar('0')));
    m_flashAddrLbl->setStyleSheet("color: #555; font-size: 11px;");

    fileLayout->addLayout(fileRow);
    fileLayout->addWidget(m_flashAddrLbl);
    root->addWidget(fileGroup);

    // ── 进度条 ────────────────────────────────────────────────
    m_progress = new QProgressBar;
    m_progress->setRange(0, 100);
    m_progress->setValue(0);
    m_progress->setTextVisible(true);
    root->addWidget(m_progress);

    // ── 日志 ──────────────────────────────────────────────────
    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setMaximumBlockCount(500);
    QFont logFont("Courier New", 9);
    logFont.setStyleHint(QFont::Monospace);
    m_log->setFont(logFont);
    m_log->setMinimumHeight(140);
    root->addWidget(m_log, 1);

    // ── 按钮行 ────────────────────────────────────────────────
    auto* btnRow = new QHBoxLayout;
    btnRow->addStretch();
    m_btnDownload = new QPushButton("Download");
    m_btnDownload->setDefault(true);
    m_btnDownload->setMinimumWidth(100);
    m_btnClose = new QPushButton("Close");
    m_btnClose->setMinimumWidth(80);
    btnRow->addWidget(m_btnDownload);
    btnRow->addWidget(m_btnClose);
    root->addLayout(btnRow);

    // ── 初始化端口列表 ────────────────────────────────────────
    onRefreshPorts();

    // ── 信号连接 ──────────────────────────────────────────────
    connect(btnBrowse,          &QPushButton::clicked,       this, &DownloadDialog::onBrowse);
    connect(m_btnRefresh,       &QPushButton::clicked,       this, &DownloadDialog::onRefreshPorts);
    connect(m_btnDownload,      &QPushButton::clicked,       this, &DownloadDialog::onDownload);
    connect(m_btnClose,         &QPushButton::clicked,       this, &QDialog::reject);
    connect(m_transportTabs,    &QTabWidget::currentChanged, this, &DownloadDialog::onTransportTabChanged);
}

// ─────────────────────────────────────────────────────────────────────────────
// 槽实现
// ─────────────────────────────────────────────────────────────────────────────
void DownloadDialog::onBrowse()
{
    QString path = QFileDialog::getOpenFileName(
        this, "Select Binary File", QString(),
        "PLC Download Images (*.bin *.xcode.bin);;All Files (*)");
    if (!path.isEmpty())
        m_binPathEdit->setText(path);
}

void DownloadDialog::onRefreshPorts()
{
    m_portCombo->clear();
    for (const QString& p : SerialTransport::availablePorts())
        m_portCombo->addItem(p);
    if (m_portCombo->count() == 0)
        m_portCombo->addItem("(no ports found)");
}

void DownloadDialog::onTransportTabChanged(int /*index*/)
{
    // 为将来启用/禁用 Ethernet 做预留
}

void DownloadDialog::onDownload()
{
    // 读取 binary 文件
    QString binPath = m_binPathEdit->text().trimmed();
    if (binPath.isEmpty()) {
        QMessageBox::warning(this, "Download", "Please select a binary file.");
        return;
    }
    QFile f(binPath);
    if (!f.open(QIODevice::ReadOnly)) {
        QMessageBox::critical(this, "Download",
            QString("Cannot open file:\n%1").arg(f.errorString()));
        return;
    }
    QByteArray binData = f.readAll();
    f.close();

    if (binData.isEmpty()) {
        QMessageBox::warning(this, "Download", "Binary file is empty.");
        return;
    }
    if (!validateBinary(binPath, binData))
        return;

    // 创建传输层
    delete m_protocol;  m_protocol  = nullptr;
    delete m_transport; m_transport = nullptr;

    if (m_transportTabs->currentIndex() == 0) {
        // Serial
        if (m_portCombo->currentText().startsWith("(")) {
            QMessageBox::warning(this, "Download", "No serial port available.");
            return;
        }
        auto* serial = new SerialTransport(this);
        serial->setPort(m_portCombo->currentText());
        serial->setBaudRate(m_baudCombo->currentData().toInt());
        m_transport = serial;
    } else {
        // TCP
        auto* tcp = new TcpTransport(this);
        tcp->setHost(m_hostEdit->text().trimmed());
        tcp->setPort(m_tcpPortSpin->value());
        m_transport = tcp;
    }

    // 打开传输
    appendLog(QString("[%1] Opening %2 ...")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(m_transport->displayName()));

    if (!m_transport->open()) {
        appendLog("[ERROR] Failed to open transport.");
        QMessageBox::critical(this, "Download",
            QString("Cannot open transport:\n%1").arg(m_transport->displayName()));
        delete m_transport; m_transport = nullptr;
        return;
    }

    // 创建协议
    m_protocol = new PlcProtocol(m_transport, this);
    connect(m_protocol, &PlcProtocol::logMessage,
            this, &DownloadDialog::appendLog);
    connect(m_protocol, &PlcProtocol::downloadProgress,
            this, &DownloadDialog::onProgress);
    connect(m_protocol, &PlcProtocol::downloadComplete,
            this, &DownloadDialog::onDownloadComplete);
    connect(m_protocol, &PlcProtocol::downloadFailed,
            this, &DownloadDialog::onDownloadFailed);

    // 连接 Abort 按钮（下载期间变成 Abort）
    disconnect(m_btnDownload, nullptr, nullptr, nullptr);
    connect(m_btnDownload, &QPushButton::clicked, this, &DownloadDialog::onAbort);
    m_btnDownload->setText("Abort");

    m_progress->setValue(0);
    setUiBusy(true);

    // 启动下载
    m_protocol->downloadBinary(binData);
}

void DownloadDialog::onAbort()
{
    if (m_protocol) m_protocol->abort();
    if (m_transport) { m_transport->close(); }
    // 恢复按钮
    disconnect(m_btnDownload, nullptr, nullptr, nullptr);
    connect(m_btnDownload, &QPushButton::clicked, this, &DownloadDialog::onDownload);
    m_btnDownload->setText("Download");
    setUiBusy(false);
}

// ─────────────────────────────────────────────────────────────────────────────
// 辅助
// ─────────────────────────────────────────────────────────────────────────────
quint32 DownloadDialog::readLe32(const QByteArray& data, int offset)
{
    if (offset < 0 || data.size() < offset + 4)
        return 0;
    return static_cast<quint32>(static_cast<unsigned char>(data[offset]))
         | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 1])) << 8)
         | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 2])) << 16)
         | (static_cast<quint32>(static_cast<unsigned char>(data[offset + 3])) << 24);
}

DownloadDialog::BinaryKind
DownloadDialog::inferBinaryKind(const QString& path, const QByteArray& data) const
{
    const QString suffix = QFileInfo(path).fileName().toLower();
    const quint32 magic = readLe32(data, 0);

    if (suffix.endsWith(".xcode.bin") || magic == PlcProtocol::XCODE_WASM_MAGIC)
        return BinaryKind::XcodeImage;
    if (suffix.endsWith(".bin") || magic == PlcProtocol::USER_LOGIC_MAGIC)
        return BinaryKind::NccImage;
    return BinaryKind::Unknown;
}

bool DownloadDialog::validateBinary(const QString& path, const QByteArray& data)
{
    const BinaryKind kind = inferBinaryKind(path, data);
    if (kind == BinaryKind::Unknown) {
        QMessageBox::critical(this, "Download",
            "Unsupported download file. Select a .bin NCC image or a .xcode.bin XCODE image.");
        return false;
    }

    if (data.size() > static_cast<int>(PlcProtocol::USER_FLASH_SIZE)) {
        QMessageBox::critical(this, "Download",
            QString("Binary is too large for UserLogic B partition.\n\n"
                    "File: %1 bytes\nLimit: %2 bytes")
                .arg(data.size())
                .arg(PlcProtocol::USER_FLASH_SIZE));
        return false;
    }

    if (kind == BinaryKind::NccImage) {
        const quint32 magic = readLe32(data, 0);
        if (magic != PlcProtocol::USER_LOGIC_MAGIC) {
            QMessageBox::critical(this, "Download",
                QString("Invalid NCC image.\n\n"
                        "Expected UserLogic magic 0x%1 at offset 0.\n"
                        "Actual: 0x%2")
                    .arg(PlcProtocol::USER_LOGIC_MAGIC, 8, 16, QChar('0'))
                    .arg(magic, 8, 16, QChar('0')));
            return false;
        }
        return true;
    }

    if (kind == BinaryKind::XcodeImage) {
        const quint32 magic = readLe32(data, 0);
        const quint32 wasmSize = readLe32(data, 4);
        if (magic != PlcProtocol::XCODE_WASM_MAGIC) {
            QMessageBox::critical(this, "Download",
                QString("Invalid XCODE image.\n\n"
                        "Expected XCODE magic 0x%1 at offset 0.\n"
                        "Actual: 0x%2")
                    .arg(PlcProtocol::XCODE_WASM_MAGIC, 8, 16, QChar('0'))
                    .arg(magic, 8, 16, QChar('0')));
            return false;
        }
        if (wasmSize == 0 || data.size() != static_cast<int>(wasmSize + 8u)) {
            QMessageBox::critical(this, "Download",
                QString("Invalid XCODE image size.\n\n"
                        "Header wasm size: %1 bytes\n"
                        "File size: %2 bytes")
                    .arg(wasmSize)
                    .arg(data.size()));
            return false;
        }
        return true;
    }

    return false;
}

void DownloadDialog::setUiBusy(bool busy)
{
    m_transportTabs->setEnabled(!busy);
    m_binPathEdit->setEnabled(!busy);
    m_btnClose->setEnabled(!busy);
}

void DownloadDialog::appendLog(const QString& msg)
{
    m_log->appendPlainText(msg);
    m_log->ensureCursorVisible();
}

void DownloadDialog::onProgress(int page, int total)
{
    int pct = (total > 0) ? (page * 100 / total) : 0;
    m_progress->setValue(pct);
    m_progress->setFormat(QString("Writing %1/%2 pages  (%3%)")
                          .arg(page).arg(total).arg(pct));
}

void DownloadDialog::onDownloadComplete()
{
    m_progress->setValue(100);
    m_progress->setFormat("Done");

    if (m_transport) m_transport->close();

    disconnect(m_btnDownload, nullptr, nullptr, nullptr);
    connect(m_btnDownload, &QPushButton::clicked, this, &DownloadDialog::onDownload);
    m_btnDownload->setText("Download");
    setUiBusy(false);

    appendLog(QString("[%1] Transfer successful.")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss")));
    QMessageBox::information(this, "Download", "Program downloaded successfully!\nPLC has been restarted.");
}

void DownloadDialog::onDownloadFailed(const QString& reason)
{
    if (m_transport) m_transport->close();

    disconnect(m_btnDownload, nullptr, nullptr, nullptr);
    connect(m_btnDownload, &QPushButton::clicked, this, &DownloadDialog::onDownload);
    m_btnDownload->setText("Download");
    setUiBusy(false);

    appendLog(QString("[%1] FAILED: %2")
              .arg(QDateTime::currentDateTime().toString("hh:mm:ss"))
              .arg(reason));
    QMessageBox::critical(this, "Download Failed", reason);
}
