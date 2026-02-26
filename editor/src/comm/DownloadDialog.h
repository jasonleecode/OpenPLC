#pragma once
#include <QDialog>

class QTabWidget;
class QComboBox;
class QSpinBox;
class QLineEdit;
class QProgressBar;
class QPlainTextEdit;
class QPushButton;
class QLabel;
class IPlcTransport;
class PlcProtocol;

// ─────────────────────────────────────────────────────────────────────────────
// DownloadDialog — 下载程序到 PLC 运行时
//
// 布局：
//   ┌ 传输方式 ──────────────────────────┐
//   │ [Serial | Ethernet]                │
//   │  串口: Port [COM3▼][刷新] Baud[▼]  │
//   │  以太网: Host [___] Port [6699]    │
//   └────────────────────────────────────┘
//   Binary:  [/path/user_logic.bin] [浏览]
//   Flash 基址: 0x00004000 (只读)
//   Progress: [██████░░░░░░░░░░░░░░ 30%]
//   Log:      [ 多行日志文本 ]
//             [Download]  [Close]
//
// 传输层通过 IPlcTransport 接口抽象，扩展以太网时只需切换实例。
// ─────────────────────────────────────────────────────────────────────────────
class DownloadDialog : public QDialog {
    Q_OBJECT
public:
    explicit DownloadDialog(QWidget* parent = nullptr);

    // 预填充 binary 路径（如果编译器输出已知路径可传入）
    void setBinaryPath(const QString& path);

private:
    void setupUi();

    // 槽
    void onBrowse();
    void onDownload();
    void onAbort();
    void onRefreshPorts();
    void onTransportTabChanged(int index);

    // 帮助方法
    void setUiBusy(bool busy);
    void appendLog(const QString& msg);
    void onProgress(int page, int total);
    void onDownloadComplete();
    void onDownloadFailed(const QString& reason);

    // ── 传输配置 ──────────────────────────────────────────────
    QTabWidget*  m_transportTabs = nullptr;

    // Serial 标签页
    QComboBox*   m_portCombo     = nullptr;
    QComboBox*   m_baudCombo     = nullptr;
    QPushButton* m_btnRefresh    = nullptr;

    // Ethernet 标签页
    QLineEdit*   m_hostEdit      = nullptr;
    QSpinBox*    m_tcpPortSpin   = nullptr;

    // ── 文件选择 ──────────────────────────────────────────────
    QLineEdit*   m_binPathEdit   = nullptr;
    QLabel*      m_flashAddrLbl  = nullptr;

    // ── 进度 ──────────────────────────────────────────────────
    QProgressBar*   m_progress   = nullptr;
    QPlainTextEdit* m_log        = nullptr;

    // ── 按钮 ──────────────────────────────────────────────────
    QPushButton* m_btnDownload   = nullptr;
    QPushButton* m_btnClose      = nullptr;

    // ── 协议 ──────────────────────────────────────────────────
    IPlcTransport* m_transport   = nullptr;
    PlcProtocol*   m_protocol    = nullptr;
};
