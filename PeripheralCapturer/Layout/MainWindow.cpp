#include "MainWindow.h"

#include "../Input/GamepadProbe.h"
#include "../Input/InputPipeline.h"
#include "../overlay/PovWindow.h"
#include "../storage/Database.h"
#include "../storage/Recorder.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFont>
#include <QFormLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMessageBox>
#include <QPushButton>
#include <QSignalBlocker>
#include <QStatusBar>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QUrl>

#include <spdlog/spdlog.h>

#include <utility>
#include <vector>

MainWindow::MainWindow(Database& database, InputPipeline& pipeline, Recorder& recorder, QWidget* parent)
    : QMainWindow(parent)
    , database_(database)
    , pipeline_(pipeline)
    , recorder_(recorder) {
    ui.setupUi(this);
    setupChrome();
    setupRecordTargets();
    setupKeyCodesPage();

    connect(ui.LeftSideBar, &QListWidget::currentRowChanged,
            ui.stackedWidget, &QStackedWidget::setCurrentIndex);
    ui.LeftSideBar->setCurrentRow(PageDevices);

    connect(&padTimer_, &QTimer::timeout, this, &MainWindow::pollGamepads);
    padTimer_.start(1000);
    pollGamepads();

    spdlog::info("[ui] MainWindow init bits={:#x}", recordingDeviceBits());
}

MainWindow::~MainWindow() {
    stopBindListen({});
    spdlog::info("[ui] MainWindow destroyed");
}

void MainWindow::setupChrome() {
    setWindowTitle(QStringLiteral("Peripheral Capturer"));

    ui.LeftSideBar->setFixedWidth(200);
    ui.LeftSideBar->setSpacing(2);
    ui.LeftSideBar->setStyleSheet(QStringLiteral(
        "QListWidget { background: #2b2b2b; color: #e8e8e8; padding: 8px 0; }"
        "QListWidget::item { min-height: 36px; padding-left: 16px; }"
        "QListWidget::item:selected { background: #3d6b99; }"
        "QListWidget::item:hover { background: #3a3a3a; }"));

    const auto applyTitle = [](QLabel* title) {
        QFont font = title->font();
        font.setPointSize(16);
        font.setBold(true);
        title->setFont(font);
    };
    applyTitle(ui.titleDevices);
    applyTitle(ui.titleKeyCodes);
    applyTitle(ui.titleProfile);
    applyTitle(ui.titleRecordings);
    applyTitle(ui.titleLog);

    ui.hintDevices->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintKeyCodes->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintProfile->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintRecordings->setStyleSheet(QStringLiteral("color: #888;"));
    ui.hintLog->setStyleSheet(QStringLiteral("color: #888;"));

#ifndef NDEBUG
    applyTitle(ui.titleDebug);
    ui.hintDebug->setStyleSheet(QStringLiteral("color: #888;"));
    setupDebugPage();
#else
    // Release 不要出现调试入口；stacked 里仍留着 pageDebug，只是点不到。
    for (int i = 0; i < ui.LeftSideBar->count(); ++i) {
        if (ui.LeftSideBar->item(i)->text() == QStringLiteral("调试")) {
            delete ui.LeftSideBar->takeItem(i);
            break;
        }
    }
    ui.pageDebug->hide();
#endif

    statusBar()->showMessage(QStringLiteral("就绪"));
}

void MainWindow::setupRecordTargets() {
    if (!ui.recordKeyboard || !ui.recordMouse || !ui.recordGamepad) {
        spdlog::error("[ui] record target checkboxes missing, rebuild MainWindow.ui");
        return;
    }
    appConfig_ = AppConfig::load();
    applyRecordTargetsToUi();
    connect(ui.recordKeyboard, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
    connect(ui.recordMouse, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
    connect(ui.recordGamepad, &QCheckBox::toggled, this, &MainWindow::onRecordTargetChanged);
}

void MainWindow::applyRecordTargetsToUi() {
    const QSignalBlocker b1(ui.recordKeyboard);
    const QSignalBlocker b2(ui.recordMouse);
    const QSignalBlocker b3(ui.recordGamepad);
    ui.recordKeyboard->setChecked(appConfig_.recordKeyboard);
    ui.recordMouse->setChecked(appConfig_.recordMouse);
    ui.recordGamepad->setChecked(appConfig_.recordGamepad);
}

uint16_t MainWindow::recordingDeviceBits() const {
    return appConfig_.recordingDeviceBits();
}

void MainWindow::onRecordTargetChanged() {
    appConfig_.recordKeyboard = ui.recordKeyboard->isChecked();
    appConfig_.recordMouse = ui.recordMouse->isChecked();
    appConfig_.recordGamepad = ui.recordGamepad->isChecked();
    if (appConfig_.recordingDeviceBits() == 0) {
        spdlog::warn("[ui] record targets empty, keep last valid");
        applyRecordTargetsToUi();
        return;
    }
    appConfig_.save();
}

namespace {

QTableWidgetItem* textItem(const QString& text) {
    auto* item = new QTableWidgetItem(text);
    item->setFlags(Qt::ItemIsSelectable | Qt::ItemIsEnabled);
    return item;
}

QString nativeVkText(const std::optional<int>& vk) {
    if (!vk) {
        return {};
    }
    return QStringLiteral("0x%1").arg(*vk, 2, 16, QLatin1Char('0')).toUpper();
}

bool promptNewKeyCode(QWidget* parent, QString& keyId, QString& kind, QString& valueKind,
                      QString& label, std::optional<double>& rangeMin,
                      std::optional<double>& rangeMax) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("注册码本条目"));
    auto* form = new QFormLayout(&dlg);
    auto* keyEdit = new QLineEdit(&dlg);
    keyEdit->setPlaceholderText(QStringLiteral("如 extra-1"));
    auto* labelEdit = new QLineEdit(&dlg);
    auto* kindBox = new QComboBox(&dlg);
    kindBox->addItem(QStringLiteral("键盘"), QStringLiteral("keyboard"));
    kindBox->addItem(QStringLiteral("鼠标"), QStringLiteral("mouse"));
    kindBox->addItem(QStringLiteral("手柄"), QStringLiteral("gamepad"));
    auto* valueBox = new QComboBox(&dlg);
    valueBox->addItem(QStringLiteral("数字键"), QStringLiteral("digital"));
    valueBox->addItem(QStringLiteral("线性轴"), QStringLiteral("analog"));
    auto* minSpin = new QDoubleSpinBox(&dlg);
    minSpin->setDecimals(3);
    minSpin->setRange(-100.0, 100.0);
    minSpin->setValue(0.0);
    auto* maxSpin = new QDoubleSpinBox(&dlg);
    maxSpin->setDecimals(3);
    maxSpin->setRange(-100.0, 100.0);
    maxSpin->setValue(1.0);
    form->addRow(QStringLiteral("key_id"), keyEdit);
    form->addRow(QStringLiteral("标签"), labelEdit);
    form->addRow(QStringLiteral("类型"), kindBox);
    form->addRow(QStringLiteral("值"), valueBox);
    form->addRow(QStringLiteral("range_min"), minSpin);
    form->addRow(QStringLiteral("range_max"), maxSpin);
    const auto syncRange = [valueBox, minSpin, maxSpin] {
        const bool analog = valueBox->currentData().toString() == QLatin1String("analog");
        minSpin->setEnabled(analog);
        maxSpin->setEnabled(analog);
    };
    QObject::connect(valueBox, &QComboBox::currentIndexChanged, &dlg, syncRange);
    syncRange();
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    keyId = keyEdit->text();
    label = labelEdit->text();
    kind = kindBox->currentData().toString();
    valueKind = valueBox->currentData().toString();
    if (valueKind == QLatin1String("analog")) {
        rangeMin = minSpin->value();
        rangeMax = maxSpin->value();
    } else {
        rangeMin.reset();
        rangeMax.reset();
    }
    return true;
}

bool promptKeyMeta(QWidget* parent, const KeyCodeRecord& row, QString& label,
                   std::optional<double>& rangeMin, std::optional<double>& rangeMax) {
    QDialog dlg(parent);
    dlg.setWindowTitle(QStringLiteral("改标签"));
    auto* form = new QFormLayout(&dlg);
    form->addRow(QStringLiteral("key_id"), new QLabel(row.keyId, &dlg));
    auto* labelEdit = new QLineEdit(row.defaultLabel, &dlg);
    form->addRow(QStringLiteral("标签"), labelEdit);
    QDoubleSpinBox* minSpin = nullptr;
    QDoubleSpinBox* maxSpin = nullptr;
    const bool analog = row.valueKind == QLatin1String("analog");
    if (analog) {
        minSpin = new QDoubleSpinBox(&dlg);
        maxSpin = new QDoubleSpinBox(&dlg);
        minSpin->setDecimals(3);
        maxSpin->setDecimals(3);
        minSpin->setRange(-100.0, 100.0);
        maxSpin->setRange(-100.0, 100.0);
        minSpin->setValue(row.rangeMin.value_or(0.0));
        maxSpin->setValue(row.rangeMax.value_or(1.0));
        form->addRow(QStringLiteral("range_min"), minSpin);
        form->addRow(QStringLiteral("range_max"), maxSpin);
    }
    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
    form->addRow(buttons);
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    if (dlg.exec() != QDialog::Accepted) {
        return false;
    }
    label = labelEdit->text();
    if (analog) {
        rangeMin = minSpin->value();
        rangeMax = maxSpin->value();
    } else {
        rangeMin.reset();
        rangeMax.reset();
    }
    return true;
}

} // namespace

void MainWindow::setupKeyCodesPage() {
    ui.tableKeyCodes->setColumnCount(6);
    ui.tableKeyCodes->setHorizontalHeaderLabels(
        {QStringLiteral("key_id"), QStringLiteral("kind"), QStringLiteral("value"),
         QStringLiteral("native_vk"), QStringLiteral("标签"), QStringLiteral("origin")});
    ui.tableKeyCodes->verticalHeader()->setVisible(false);
    ui.tableKeyCodes->setShowGrid(false);
    ui.tableKeyCodes->setWordWrap(false);
    ui.tableKeyCodes->horizontalHeader()->setStretchLastSection(true);
    ui.tableKeyCodes->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);

    bindQueue_ = pipeline_.bus().subscribe(bindListenSubscribeOptions());
    bindListenTimer_.setInterval(20);
    connect(&bindListenTimer_, &QTimer::timeout, this, &MainWindow::pollBindListen);
    connect(ui.btnKeyCodeAdd, &QPushButton::clicked, this, &MainWindow::onKeyCodeAdd);
    connect(ui.btnKeyCodeEdit, &QPushButton::clicked, this, &MainWindow::onKeyCodeEdit);
    connect(ui.btnKeyCodeBind, &QPushButton::clicked, this, &MainWindow::onKeyCodeBind);
    connect(ui.btnKeyCodeDelete, &QPushButton::clicked, this, &MainWindow::onKeyCodeDelete);
    connect(ui.tableKeyCodes, &QTableWidget::itemSelectionChanged, this,
            &MainWindow::updateKeyCodeActions);

    refreshKeyCodesTable();
    reloadPipelineCodebook();
}

void MainWindow::reloadPipelineCodebook() {
    std::vector<InputPipeline::NativeVkBinding> bindings;
    const auto rows = database_.listKeyCodes();
    bindings.reserve(rows.size());
    for (const auto& row : rows) {
        if (!row.nativeVk) {
            continue;
        }
        if (row.kind != QLatin1String("keyboard") && row.kind != QLatin1String("mouse")) {
            continue;
        }
        InputPipeline::NativeVkBinding b;
        b.kind = row.kind.toStdString();
        b.nativeVk = *row.nativeVk;
        b.keyId = row.keyId.toStdString();
        bindings.push_back(std::move(b));
    }
    pipeline_.reloadNativeVkMap(std::move(bindings));
}

void MainWindow::refreshKeyCodesTable() {
    const auto keepId = selectedKeyCodeId();
    const QSignalBlocker blocker(ui.tableKeyCodes);
    ui.tableKeyCodes->setRowCount(0);
    const auto rows = database_.listKeyCodes();
    ui.tableKeyCodes->setRowCount(static_cast<int>(rows.size()));
    int restore = -1;
    for (int i = 0; i < static_cast<int>(rows.size()); ++i) {
        const auto& row = rows[static_cast<std::size_t>(i)];
        auto* idItem = textItem(row.keyId);
        idItem->setData(Qt::UserRole, row.id);
        ui.tableKeyCodes->setItem(i, 0, idItem);
        ui.tableKeyCodes->setItem(i, 1, textItem(row.kind));
        ui.tableKeyCodes->setItem(i, 2, textItem(row.valueKind));
        ui.tableKeyCodes->setItem(i, 3, textItem(nativeVkText(row.nativeVk)));
        ui.tableKeyCodes->setItem(i, 4, textItem(row.defaultLabel));
        ui.tableKeyCodes->setItem(i, 5, textItem(row.origin));
        if (keepId && row.id == *keepId) {
            restore = i;
        }
    }
    if (restore >= 0) {
        ui.tableKeyCodes->selectRow(restore);
    }
    updateKeyCodeActions();
}

void MainWindow::updateKeyCodeActions() {
    const auto row = selectedKeyCode();
    const bool has = row.has_value();
    ui.btnKeyCodeAdd->setEnabled(!bindListening_);
    ui.btnKeyCodeEdit->setEnabled(has && !bindListening_);
    ui.btnKeyCodeBind->setEnabled(bindListening_ || has);
    ui.btnKeyCodeDelete->setEnabled(has && !bindListening_ &&
                                    row->origin != QLatin1String("builtin"));
    ui.tableKeyCodes->setEnabled(!bindListening_);
}

std::optional<qint64> MainWindow::selectedKeyCodeId() const {
    const int row = ui.tableKeyCodes->currentRow();
    if (row < 0) {
        return std::nullopt;
    }
    const auto* item = ui.tableKeyCodes->item(row, 0);
    if (!item) {
        return std::nullopt;
    }
    const qint64 id = item->data(Qt::UserRole).toLongLong();
    if (id <= 0) {
        return std::nullopt;
    }
    return id;
}

std::optional<KeyCodeRecord> MainWindow::selectedKeyCode() const {
    const auto id = selectedKeyCodeId();
    if (!id) {
        return std::nullopt;
    }
    return database_.findById(*id);
}

void MainWindow::onKeyCodeAdd() {
    QString keyId;
    QString kind;
    QString valueKind;
    QString label;
    std::optional<double> rangeMin;
    std::optional<double> rangeMax;
    if (!promptNewKeyCode(this, keyId, kind, valueKind, label, rangeMin, rangeMax)) {
        return;
    }
    const auto id = database_.insertUserKey(keyId, kind, valueKind, label, rangeMin, rangeMax);
    if (!id) {
        QMessageBox::warning(this, QStringLiteral("注册失败"),
                             QStringLiteral("key_id 须为小写字母数字和 '-'，且不能与已有条目重复。"));
        return;
    }
    refreshKeyCodesTable();
    reloadPipelineCodebook();
    statusBar()->showMessage(QStringLiteral("已注册 %1").arg(keyId.trimmed().toLower()), 4000);
}

void MainWindow::onKeyCodeEdit() {
    const auto row = selectedKeyCode();
    if (!row) {
        return;
    }
    QString label;
    std::optional<double> rangeMin;
    std::optional<double> rangeMax;
    if (!promptKeyMeta(this, *row, label, rangeMin, rangeMax)) {
        return;
    }
    if (!database_.updateKeyMeta(row->id, label, rangeMin, rangeMax)) {
        QMessageBox::warning(this, QStringLiteral("保存失败"),
                             QStringLiteral("标签不能为空；线性轴需要 range_min < range_max。"));
        return;
    }
    refreshKeyCodesTable();
    statusBar()->showMessage(QStringLiteral("已更新 %1").arg(row->keyId), 4000);
}

void MainWindow::onKeyCodeDelete() {
    const auto row = selectedKeyCode();
    if (!row) {
        return;
    }
    if (row->origin == QLatin1String("builtin")) {
        return;
    }
    const auto ret = QMessageBox::question(
        this, QStringLiteral("删除码本条目"),
        QStringLiteral("删除 %1（%2）？").arg(row->keyId, row->origin),
        QMessageBox::Ok | QMessageBox::Cancel, QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) {
        return;
    }
    if (!database_.deleteKeyCode(row->id)) {
        QMessageBox::warning(this, QStringLiteral("删除失败"),
                             QStringLiteral("预置条目不能删。"));
        return;
    }
    refreshKeyCodesTable();
    reloadPipelineCodebook();
    statusBar()->showMessage(QStringLiteral("已删除 %1").arg(row->keyId), 4000);
}

void MainWindow::onKeyCodeBind() {
    if (bindListening_) {
        stopBindListen(QStringLiteral("已取消监听"));
        return;
    }
    const auto row = selectedKeyCode();
    if (!row) {
        return;
    }
    if (row->kind == QLatin1String("gamepad")) {
        QMessageBox::information(this, QStringLiteral("无需绑定"),
                                 QStringLiteral("手柄用 key_id 识别，无需 native_vk。"));
        return;
    }
    if (row->valueKind != QLatin1String("digital") ||
        (row->kind != QLatin1String("keyboard") && row->kind != QLatin1String("mouse"))) {
        QMessageBox::information(this, QStringLiteral("无法监听"),
                                 QStringLiteral("只对键盘/鼠标数字键监听绑定 native_vk。轴不绑 VK。"));
        return;
    }
    startBindListen(row->id);
}

void MainWindow::startBindListen(qint64 id) {
    if (!bindQueue_) {
        spdlog::error("[ui] bind queue missing");
        return;
    }
    while (bindQueue_->tryPop()) {
    }
    bindListening_ = true;
    bindListenTargetId_ = id;
    bindListenUntilMs_ = QDateTime::currentMSecsSinceEpoch() + 8000;
    ui.btnKeyCodeBind->setText(QStringLiteral("取消监听"));
    updateKeyCodeActions();
    bindListenTimer_.start();
    statusBar()->showMessage(QStringLiteral("请按下要绑定的键（8 秒）"));
    spdlog::info("[ui] codebook listen start id={}", id);
}

void MainWindow::stopBindListen(const QString& status) {
    if (!bindListening_) {
        return;
    }
    bindListening_ = false;
    bindListenTimer_.stop();
    bindListenTargetId_ = 0;
    ui.btnKeyCodeBind->setText(QStringLiteral("监听绑定"));
    updateKeyCodeActions();
    if (!status.isEmpty()) {
        statusBar()->showMessage(status, 4000);
    }
}

void MainWindow::pollBindListen() {
    if (!bindListening_ || !bindQueue_) {
        return;
    }
    if (QDateTime::currentMSecsSinceEpoch() >= bindListenUntilMs_) {
        stopBindListen(QStringLiteral("监听超时"));
        return;
    }
    while (const auto ev = bindQueue_->tryPop()) {
        if (applyBindEvent(*ev)) {
            return;
        }
    }
}

bool MainWindow::applyBindEvent(const InputEvent& event) {
    if (event.type != InputEventType::KeyDown &&
        event.type != InputEventType::MouseButtonDown) {
        return false;
    }
    if (event.vkey == 0) {
        return false;
    }
    const qint64 id = bindListenTargetId_;
    if (!database_.bindNativeVk(id, event.vkey)) {
        stopBindListen(QStringLiteral("绑定失败"));
        return true;
    }
    reloadPipelineCodebook();
    refreshKeyCodesTable();
    stopBindListen(
        QStringLiteral("已绑定 native_vk=%1").arg(nativeVkText(static_cast<int>(event.vkey))));
    return true;
}

void MainWindow::pollGamepads() {
    enumerateHidGamepads(registry_);
    pollXInputSlots(registry_, xinputConnected_);
    refreshGamepadList();
}

void MainWindow::refreshGamepadList() {
    ui.deviceList->clear();
    int xinputCount = 0;
    for (DWORD slot = 0; slot < 4; ++slot) {
        const auto id = registry_.getXInputDeviceID(slot);
        if (xinputConnected_[slot]) {
            ++xinputCount;
            ui.deviceList->addItem(QString::fromStdString(id + "  已连接"));
        } else {
            ui.deviceList->addItem(QString::fromStdString(id + "  空"));
        }
    }
    int hidCount = 0;
    for (const auto& info : registry_.snapshot()) {
        if (info.type != InputDeviceType::Hid) {
            continue;
        }
        ++hidCount;
        QString line = QString::fromStdString(info.deviceID);
        if (info.likelyXInput) {
            line += QStringLiteral("  （IG_，走 XInput）");
        }
        ui.deviceList->addItem(line);
    }
    statusBar()->showMessage(
        QStringLiteral("XInput %1/4  ·  HID 手柄 %2").arg(xinputCount).arg(hidCount));
}

void MainWindow::setPovWindow(PovWindow* pov) {
    pov_ = pov;
#ifndef NDEBUG
    refreshDebugStats();
#endif
}

#ifndef NDEBUG
void MainWindow::setupDebugPage() {
    connect(ui.debugRebuildDb, &QPushButton::clicked, this, &MainWindow::onDebugRebuildDatabase);
    connect(ui.debugResetAppConfig, &QPushButton::clicked, this, &MainWindow::onDebugResetAppConfig);
    connect(ui.debugOpenAppDir, &QPushButton::clicked, this, &MainWindow::onDebugOpenAppDir);
    connect(ui.debugOpenLogDir, &QPushButton::clicked, this, &MainWindow::onDebugOpenLogDir);
    connect(ui.debugTogglePovClick, &QPushButton::clicked, this, &MainWindow::onDebugTogglePovClick);
    connect(ui.debugToggleRecord, &QPushButton::clicked, this, &MainWindow::onDebugToggleRecord);
    connect(&debugTimer_, &QTimer::timeout, this, [this] {
        if (ui.LeftSideBar->currentRow() == PageDebug) {
            refreshDebugStats();
        }
    });
    debugTimer_.start(1000);
    refreshDebugStats();
}

void MainWindow::refreshDebugStats() {
    const auto st = database_.stats();
    const QString path = st.path.isEmpty() ? QStringLiteral("（无路径）") : st.path;
    const QString povLine = pov_
        ? QStringLiteral("POV 穿透：%1").arg(pov_->clickThrough() ? QStringLiteral("开")
                                                                   : QStringLiteral("关"))
        : QStringLiteral("POV：尚未挂上");
    // 不要用 %10：链式 arg 会把 %10 当成 %1。
    ui.debugStats->setText(
        QStringLiteral("库：%1\n").arg(path) +
        QStringLiteral("open=%1  schema=%2  key_codes=%3  sessions=%4  frames=%5  markers=%6\n")
            .arg(st.open ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(st.schemaVersion)
            .arg(st.keyCodes)
            .arg(st.sessions)
            .arg(st.frames)
            .arg(st.markers) +
        QStringLiteral("采集：running=%1  published=%2  packetDropped=%3\n")
            .arg(pipeline_.running() ? QStringLiteral("yes") : QStringLiteral("no"))
            .arg(pipeline_.publishedCount())
            .arg(static_cast<qulonglong>(pipeline_.packetDropped())) +
        QStringLiteral("录制：%1  session=%2  fps=%3\n")
            .arg(recorder_.recording() ? QStringLiteral("on") : QStringLiteral("off"))
            .arg(recorder_.sessionId())
            .arg(appConfig_.recordingFps) +
        povLine);
}

void MainWindow::onDebugRebuildDatabase() {
    const auto ret = QMessageBox::warning(
        this,
        QStringLiteral("删除并重建数据库"),
        QStringLiteral("将关掉连接并删除 data.db（含 WAL）。会话、帧和自定义码本都会没，builtin 码本会重新种上。\n\n确定？"),
        QMessageBox::Ok | QMessageBox::Cancel,
        QMessageBox::Cancel);
    if (ret != QMessageBox::Ok) {
        return;
    }
    recorder_.prepareForDbWipe();
    if (!database_.recreate()) {
        spdlog::error("[ui] debug recreate database failed");
        QMessageBox::critical(this, QStringLiteral("重建失败"),
                              QStringLiteral("删库或重新 open 失败，看 log/。若 DB Browser 开着这个文件，先关掉。"));
        refreshDebugStats();
        return;
    }
    spdlog::info("[ui] debug database recreated");
    refreshKeyCodesTable();
    reloadPipelineCodebook();
    statusBar()->showMessage(QStringLiteral("数据库已重建"), 4000);
    refreshDebugStats();
}

void MainWindow::onDebugResetAppConfig() {
    const QString path = AppConfig::filePath();
    if (QFile::exists(path) && !QFile::remove(path)) {
        spdlog::warn("[ui] debug cannot delete app-config path={}", path.toStdString());
        QMessageBox::warning(this, QStringLiteral("恢复失败"),
                             QStringLiteral("删不掉 app-config.json。"));
        return;
    }
    appConfig_ = AppConfig::load();
    applyRecordTargetsToUi();
    spdlog::info("[ui] debug app-config reset bits={:#x}", appConfig_.recordingDeviceBits());
    statusBar()->showMessage(QStringLiteral("已恢复默认 app-config"), 4000);
    refreshDebugStats();
}

void MainWindow::onDebugOpenAppDir() {
    QDesktopServices::openUrl(
        QUrl::fromLocalFile(QCoreApplication::applicationDirPath()));
}

void MainWindow::onDebugOpenLogDir() {
    const QString logDir = QCoreApplication::applicationDirPath() + QStringLiteral("/log");
    QDir().mkpath(logDir);
    QDesktopServices::openUrl(QUrl::fromLocalFile(logDir));
}

void MainWindow::onDebugTogglePovClick() {
    if (!pov_) {
        spdlog::warn("[ui] debug toggle POV: window not set");
        return;
    }
    pov_->setClickThrough(!pov_->clickThrough());
    refreshDebugStats();
}

void MainWindow::onDebugToggleRecord() {
    recorder_.toggle();
    statusBar()->showMessage(QStringLiteral("已发送开/停录（F9）"), 2000);
    refreshDebugStats();
}
#endif
