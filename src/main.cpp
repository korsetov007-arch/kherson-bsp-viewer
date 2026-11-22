#include "app_language.hpp"
#include "ballistics.hpp"
#include "bsp_3d_view.hpp"
#include "bsp_file.hpp"
#include "file_bridge.hpp"
#include "topdown_picker.hpp"

#include <QApplication>
#include <QAction>
#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFontDatabase>
#include <QFrame>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMimeData>
#include <QMenu>
#include <QMenuBar>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QTimer>
#include <QUrl>
#include <QVBoxLayout>

#include <cmath>
#include <numbers>
#include <optional>
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

struct DeveloperConfig {
    double projectileSpeed = 0.0; // 0 = use the live LVS velocity, or 4000 fallback.
    double gravityOverride = 0.0; // 0 = read live physenv gravity from Garry's Mod.
    double aimToleranceDeg = 1.5;
    double aimStepDegPerTick = 2.0;
    QString muzzleAttachment = QStringLiteral("auto");
    QString path;
};

struct AimPlan {
    double viewYawDeg = 0.0;
    double viewPitchDeg = 0.0;
    double muzzleYawDeg = 0.0;
    double muzzlePitchDeg = 0.0;
    double flightTimeSec = 0.0;
    double elevationDeg = 0.0;
    double projectileSpeed = 4000.0;
    double gravity = 600.0;
    double gravityTerm = 0.5;
    bool lvsCompensated = false;
};

bool installBridgeFile(const QString &garrysmodDirectory, QString *error)
{
    if (!QDir(garrysmodDirectory).exists("addons")) {
        if (error) *error = "В выбранной папке нет каталога addons.";
        return false;
    }

    struct EmbeddedFile {
        const char *resource;
        const char *relativePath;
    };
    const EmbeddedFile files[] = {
        {":/tfc_bridge.lua", "addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua"},
        {":/tfc_bridge_server.lua", "addons/tfc_bridge/lua/autorun/server/tfc_bridge_server.lua"}
    };

    for (const EmbeddedFile &item : files) {
        QFile embedded(QString::fromUtf8(item.resource));
        if (!embedded.open(QIODevice::ReadOnly)) {
            if (error) *error = QString("Не удалось прочитать встроенный файл %1.")
                .arg(QString::fromUtf8(item.resource));
            return false;
        }
        const QByteArray bytes = embedded.readAll();
        const QString targetPath = QDir(garrysmodDirectory).filePath(
            QString::fromUtf8(item.relativePath));
        if (!QDir().mkpath(QFileInfo(targetPath).absolutePath())) {
            if (error) *error = "Windows не разрешила создать папку аддона.";
            return false;
        }

        QSaveFile target(targetPath);
        if (!target.open(QIODevice::WriteOnly)) {
            if (error) *error = target.errorString();
            return false;
        }
        if (target.write(bytes) != bytes.size()) {
            if (error) *error = target.errorString();
            target.cancelWriting();
            return false;
        }
        if (!target.commit()) {
            if (error) *error = target.errorString();
            return false;
        }
    }
    return true;
}

bool installBridgeElevated(const QString &garrysmodDirectory, QString *error)
{
#ifdef Q_OS_WIN
    const std::wstring executable =
        QCoreApplication::applicationFilePath().toStdWString();
    const std::wstring workingDirectory =
        QCoreApplication::applicationDirPath().toStdWString();
    const std::wstring parameters =
        QStringLiteral("--install-bridge \"%1\"")
            .arg(garrysmodDirectory).toStdWString();

    SHELLEXECUTEINFOW launch{};
    launch.cbSize = sizeof(launch);
    launch.fMask = SEE_MASK_NOCLOSEPROCESS;
    launch.lpVerb = L"runas";
    launch.lpFile = executable.c_str();
    launch.lpParameters = parameters.c_str();
    launch.lpDirectory = workingDirectory.c_str();
    launch.nShow = SW_SHOWNORMAL;
    if (!ShellExecuteExW(&launch)) {
        const DWORD code = GetLastError();
        if (error) {
            *error = code == ERROR_CANCELLED
                ? "Запрос UAC отменён."
                : QString("Не удалось запустить установщик (код Windows %1).").arg(code);
        }
        return false;
    }

    const DWORD waitResult = WaitForSingleObject(launch.hProcess, INFINITE);
    DWORD exitCode = 1;
    const bool gotExitCode =
        waitResult == WAIT_OBJECT_0 &&
        GetExitCodeProcess(launch.hProcess, &exitCode) != FALSE;
    CloseHandle(launch.hProcess);
    if (!gotExitCode || exitCode != 0) {
        if (error) *error = "Установка с правами администратора завершилась ошибкой.";
        return false;
    }
    return true;
#else
    Q_UNUSED(garrysmodDirectory);
    if (error) *error = "Повышенная установка доступна только в Windows.";
    return false;
#endif
}

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        developerConfig_ = loadDeveloperConfig();
        setWindowTitle("TFC — Tactical Fire Control");
        resize(1420, 900);
        setWindowFlag(Qt::WindowStaysOnTopHint);
        setAcceptDrops(true);
        setStyleSheet(QStringLiteral(R"(
            QMainWindow { background:#10151b; color:#e8edf2; }
            QWidget { color:#e8edf2; font-family:"Inter","Segoe UI",sans-serif; }
            QWidget#appRoot { background:#10151b; }
            QFrame#headerPanel { background:#080a07; border:1px solid #2a3024; }
            QFrame#dataCard { background:#080a07; border:1px solid #2a3024; }
            QLabel#brandTitle { color:#edf1e7; font-size:18px; font-weight:700; letter-spacing:1px; }
            QLabel#brandSubtitle { color:#a4a69a; font-size:10px; letter-spacing:2px; }
            QLabel#sectionTitle { color:#bec2b2; font-size:10px; font-weight:700; letter-spacing:1.4px; }
            QLabel#cardCaption { color:#92978a; font-size:10px; font-weight:600; letter-spacing:1px; }
            QLabel#readoutValue { color:#dfe6d5; font-family:Consolas,"Cascadia Mono",monospace; font-size:12px; }
            QLabel#connectionStatus { color:#a4b578; font-size:11px; font-weight:600; padding:6px 10px; border:1px solid #343d29; background:#10150d; }
            QPushButton { background:#29301f; border:1px solid #383e31; border-radius:2px; padding:8px 12px; color:#e4e4dd; font-size:12px; }
            QPushButton:hover { background:#373f2b; border-color:#596344; }
            QPushButton:pressed { background:#1b2116; }
            QPushButton:disabled { color:#6d7168; background:#171b14; border-color:#252a20; }
            QPushButton#openButton { background:#3a462a; border-color:#4b5935; font-weight:600; }
            QPushButton#aimButton { background:#354028; border-color:#637448; padding:11px 14px; font-weight:700; }
            QPushButton#fireButton { background:#583a24; border-color:#8b6036; padding:11px 14px; font-weight:700; }
            QPushButton#fireButton:hover { background:#70482a; border-color:#b47a43; }
            QPushButton#fireButton:disabled { background:#1b1a16; border-color:#393328; color:#777064; }
            QMenuBar { background:#080a07; color:#c6cbbd; padding:3px; }
            QMenuBar::item:selected, QMenu::item:selected { background:#29301f; }
            QMenu { background:#080a07; border:1px solid #343a2d; }
            QTabWidget::pane { border:1px solid #2a3024; background:#0c1009; }
            QTabBar::tab { background:#171c12; color:#a4a69a; border:1px solid #2a3024; border-bottom:none; padding:9px 18px; margin-right:3px; }
            QTabBar::tab:selected { background:#29301f; color:#edf1e7; border-color:#566343; }
            QStatusBar { background:#080a07; color:#a4a69a; border-top:1px solid #2a3024; }
        )"));
        auto *settingsMenu = menuBar()->addMenu("Настройки");
        QAction *gameDirectoryAction = settingsMenu->addAction("Папка Garry's Mod…");
        connect(gameDirectoryAction, &QAction::triggered, this, [this] {
            chooseGameDirectory();
        });

        auto *root = new QWidget(this);
        root->setObjectName("appRoot");
        auto *outer = new QVBoxLayout(root);
        outer->setContentsMargins(10, 9, 10, 8);
        outer->setSpacing(9);

        auto *header = new QFrame(root);
        header->setObjectName("headerPanel");
        auto *headerRow = new QHBoxLayout(header);
        headerRow->setContentsMargins(16, 10, 14, 10);
        headerRow->setSpacing(14);

        auto *brand = new QVBoxLayout();
        brand->setSpacing(2);
        auto *brandTitle = new QLabel("TFC  /  FIRE CONTROL", header);
        brandTitle->setObjectName("brandTitle");
        auto *brandSubtitle = new QLabel("SOURCE BSP  ·  LVS TELEMETRY  ·  BALLISTIC SOLUTION", header);
        brandSubtitle->setObjectName("brandSubtitle");
        brand->addWidget(brandTitle);
        brand->addWidget(brandSubtitle);
        headerRow->addLayout(brand);
        headerRow->addStretch(1);

        auto *openButton = new QPushButton("ОТКРЫТЬ BSP…", header);
        openButton->setObjectName("openButton");
        openButton->setMinimumHeight(36);
        headerRow->addWidget(openButton);

        bridgeLabel_ = new QLabel("ИГРА НЕ ПОДКЛЮЧЕНА", header);
        bridgeLabel_->setObjectName("connectionStatus");
        bridgeLabel_->setMinimumWidth(200);
        bridgeLabel_->setAlignment(Qt::AlignCenter);
        headerRow->addWidget(bridgeLabel_);
        outer->addWidget(header);

        auto *body = new QHBoxLayout();
        body->setSpacing(10);
        tabs_ = new QTabWidget(root);
        view3d_ = new bspvision::Bsp3DView(tabs_);
        topdown_ = new TopDownPicker(tabs_);
        tabs_->addTab(view3d_, "3D  /  РЕЛЬЕФ");
        tabs_->addTab(topdown_, "TACTICAL MAP  /  X-Y");
        body->addWidget(tabs_, 1);

        auto *side = new QWidget(root);
        side->setMinimumWidth(270);
        side->setMaximumWidth(318);
        auto *right = new QVBoxLayout(side);
        right->setContentsMargins(2, 0, 0, 0);
        right->setSpacing(7);

        auto addSection = [&](const QString &caption, const QString &value) {
            auto *card = new QFrame(side);
            card->setObjectName("dataCard");
            auto *cardLayout = new QVBoxLayout(card);
            cardLayout->setContentsMargins(10, 8, 10, 9);
            cardLayout->setSpacing(4);
            auto *title = new QLabel(caption, card);
            title->setObjectName("cardCaption");
            auto *readout = new QLabel(value, card);
            readout->setObjectName("readoutValue");
            readout->setWordWrap(true);
            cardLayout->addWidget(title);
            cardLayout->addWidget(readout);
            right->addWidget(card);
            return readout;
        };

        auto *panelTitle = new QLabel("LIVE SITUATION", side);
        panelTitle->setObjectName("sectionTitle");
        right->addWidget(panelTitle);
        mapLabel_ = addSection("MAP / BSP", "Карта не загружена");
        targetLabel_ = addSection("TARGET / XYZ", "Цель не выбрана");
        positionLabel_ = addSection("PLATFORM POSITION / XYZ", "Ожидание телеметрии…");
        orientationLabel_ = addSection("VIEW / WORLD YAW-PITCH", "—");
        muzzleLabel_ = addSection("MUZZLE / WORLD ANGLE", "—");
        weaponLabel_ = addSection("LVS WEAPON / READINESS", "Ожидание данных…");
        trajectoryLabel_ = addSection("BALLISTIC PREVIEW", "Выбери точку цели");

        aimButton_ = new QPushButton("F7  /  НАВЕСТИСЬ", side);
        aimButton_->setObjectName("aimButton");
        fireButton_ = new QPushButton("F8  /  ОДИНОЧНЫЙ ВЫСТРЕЛ", side);
        fireButton_->setObjectName("fireButton");
        fireButton_->setEnabled(false);
        right->addSpacing(4);
        right->addWidget(aimButton_);
        right->addWidget(fireButton_);
        auto *footnote = new QLabel(
            "ЯНТАРНАЯ ЛИНИЯ — расчётная траектория\n"
            "ОЛИВКОВАЯ МЕТКА — позиция и направление взгляда",
            side);
        footnote->setWordWrap(true);
        footnote->setStyleSheet("color:#8f9587;font-size:10px;padding:4px 2px;");
        right->addWidget(footnote);
        right->addStretch(1);

        body->addWidget(side);
        outer->addLayout(body, 1);
        setCentralWidget(root);

        connect(openButton, &QPushButton::clicked, this, [this] { chooseBsp(); });
        connect(aimButton_, &QPushButton::clicked, this, [this] { aim(); });
        connect(fireButton_, &QPushButton::clicked, this, [this] { fire(); });

        view3d_->setTargetCallback([this](double x, double y, double z) {
            setTarget(x, y, z);
        });
        topdown_->setPointCallback([this](double x, double y) {
            setTarget(x, y, targetZ_);
            topdown_->setMarker(x, y);
        });

        bridge_.setUpdatedCallback([this] { refreshBridgeStatus(); });

        QSettings settings("TFC", "TacticalFireControl");
        QString gmod = settings.value("garrysmodDirectory").toString();
        if (gmod.isEmpty() || !QDir(gmod).exists("addons"))
            gmod = detectGmodDirectory();

        if (gmod.isEmpty()) {
            bridgeLabel_->setText(
                "GMod не найден · укажи папку через меню «Настройки».");
            statusBar()->showMessage(
                "Выбери папку Garry's Mod в меню «Настройки».");
        } else {
            configureGameDirectory(gmod, false);
        }

#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        hotkeyF7Registered_ = RegisterHotKey(hwnd, 0x7F01, MOD_NOREPEAT, VK_F7) != FALSE;
        hotkeyF8Registered_ = RegisterHotKey(hwnd, 0x7F02, MOD_NOREPEAT, VK_F8) != FALSE;
        if (!hotkeyF7Registered_ || !hotkeyF8Registered_) {
            statusBar()->showMessage(
                "Не удалось занять F7/F8 глобально: возможно, клавиши уже использует другая программа.");
        }
#endif
        refreshBridgeStatus();
    }

    ~MainWindow() override
    {
#ifdef Q_OS_WIN
        const HWND hwnd = reinterpret_cast<HWND>(winId());
        if (hotkeyF7Registered_) UnregisterHotKey(hwnd, 0x7F01);
        if (hotkeyF8Registered_) UnregisterHotKey(hwnd, 0x7F02);
#endif
    }

    void loadBsp(const QString &path)
    {
        QString error;
        if (!bsp_.load(path, &error)) {
            QMessageBox::warning(this, "TFC — BSP", error);
            return;
        }

        const auto polygons = bsp_.facePolygons(false);
        view3d_->setMap(bsp_.vertices(), polygons);
        view3d_->clearTarget();
        view3d_->setTrajectory({});
        topdown_->setPolygons(polygons);
        targetSelected_ = false;
        lastAimSet_ = false;
        targetLabel_->setText("Цель не выбрана.\nКликни по поверхности в 3D-карте.");
        mapLabel_->setText(QFileInfo(path).fileName());
        refreshBridgeStatus();
        statusBar()->showMessage("Карта загружена. Выбери точку кликом по 3D-карте.");
    }

protected:
    void dragEnterEvent(QDragEnterEvent *event) override
    {
        if (event->mimeData()->hasUrls()) {
            for (const QUrl &url : event->mimeData()->urls()) {
                if (url.isLocalFile() &&
                    url.toLocalFile().endsWith(".bsp", Qt::CaseInsensitive)) {
                    event->acceptProposedAction();
                    return;
                }
            }
        }
        event->ignore();
    }

    void dropEvent(QDropEvent *event) override
    {
        for (const QUrl &url : event->mimeData()->urls()) {
            if (url.isLocalFile() &&
                url.toLocalFile().endsWith(".bsp", Qt::CaseInsensitive)) {
                loadBsp(url.toLocalFile());
                event->acceptProposedAction();
                return;
            }
        }
        event->ignore();
    }

#ifdef Q_OS_WIN
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override
    {
        Q_UNUSED(eventType);
        auto *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_HOTKEY) {
            if (msg->wParam == 0x7F01) aim();
            if (msg->wParam == 0x7F02) fire();
            if (result) *result = 0;
            return true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
#endif

private:
    static DeveloperConfig loadDeveloperConfig()
    {
        DeveloperConfig config;
        config.path = QDir(QCoreApplication::applicationDirPath()).filePath("tfc.cfg");

        if (!QFileInfo::exists(config.path)) {
            QFile file(config.path);
            if (file.open(QIODevice::WriteOnly | QIODevice::Text)) {
                const QByteArray defaults =
                    "# TFC developer-only bridge settings. Restart TFC after editing.\n"
                    "[ballistics]\n"
                    "projectile_speed=auto\n"
                    "gravity=auto\n\n"
                    "[aim]\n"
                    "tolerance_deg=1.5\n"
                    "step_deg_per_tick=2.0\n\n"
                    "[game]\n"
                    "muzzle_attachment=auto\n";
                file.write(defaults);
            }
        }

        QSettings cfg(config.path, QSettings::IniFormat);
        const QString speedText =
            cfg.value("ballistics/projectile_speed", "auto").toString().trimmed().toLower();
        bool speedOk = false;
        const double speedOverride = speedText.toDouble(&speedOk);
        config.projectileSpeed = speedOk && speedOverride > 0.0
            ? qBound(1.0, speedOverride, 30000.0) : 0.0;
        const QString gravityText =
            cfg.value("ballistics/gravity", "auto").toString().trimmed().toLower();
        bool gravityOk = false;
        const double gravityValue = gravityText.toDouble(&gravityOk);
        config.gravityOverride = gravityText.isEmpty() || gravityText == "auto" ||
            !gravityOk || gravityValue <= 0.0
            ? 0.0 : qBound(1.0, gravityValue, 3000.0);
        config.aimToleranceDeg = qBound(
            0.1, cfg.value("aim/tolerance_deg", 1.5).toDouble(), 10.0);
        config.aimStepDegPerTick = qBound(
            0.1, cfg.value("aim/step_deg_per_tick", 2.0).toDouble(), 10.0);
        config.muzzleAttachment =
            cfg.value("game/muzzle_attachment", "auto").toString().trimmed();
        if (config.muzzleAttachment.isEmpty())
            config.muzzleAttachment = "auto";
        return config;
    }

    QString detectGmodDirectory() const
    {
        QStringList steamRoots;
        const QString pf = qEnvironmentVariable("ProgramFiles(x86)");
        const QString pf64 = qEnvironmentVariable("ProgramFiles");
        if (!pf.isEmpty()) steamRoots << QDir(pf).filePath("Steam");
        if (!pf64.isEmpty()) steamRoots << QDir(pf64).filePath("Steam");
        steamRoots << QDir::home().filePath("Steam");

#ifdef Q_OS_WIN
        QSettings steamReg(
            "HKEY_CURRENT_USER\\Software\\Valve\\Steam", QSettings::NativeFormat);
        const QString steamPath = steamReg.value("SteamPath").toString();
        if (!steamPath.isEmpty())
            steamRoots.prepend(QDir::fromNativeSeparators(steamPath));
#endif

        for (const QString &root : std::as_const(steamRoots)) {
            const QString common = QDir(root).filePath(
                "steamapps/common/GarrysMod/garrysmod");
            if (QDir(common).exists("addons")) return QDir::cleanPath(common);

            QFile vdf(QDir(root).filePath("steamapps/libraryfolders.vdf"));
            if (!vdf.open(QIODevice::ReadOnly | QIODevice::Text)) continue;

            const QString text = QString::fromUtf8(vdf.readAll());
            const QRegularExpression re("\"path\"\\s*\"([^\"]+)\"");
            auto it = re.globalMatch(text);
            while (it.hasNext()) {
                const QString library =
                    QDir::fromNativeSeparators(it.next().captured(1));
                const QString candidate = QDir(library).filePath(
                    "steamapps/common/GarrysMod/garrysmod");
                if (QDir(candidate).exists("addons"))
                    return QDir::cleanPath(candidate);
            }
        }
        return {};
    }

    void chooseGameDirectory()
    {
        const QString picked = QFileDialog::getExistingDirectory(
            this, "Выбери папку garrysmod (внутри неё должна быть папка addons)");
        if (!picked.isEmpty()) configureGameDirectory(picked, true);
    }

    void configureGameDirectory(QString path, bool showInstallMessage)
    {
        path = QDir::cleanPath(path);
        if (QDir(path).exists("garrysmod/addons"))
            path = QDir(path).filePath("garrysmod");

        if (!QDir(path).exists("addons")) {
            QMessageBox::warning(
                this, "TFC", "Нужна именно папка garrysmod, внутри которой есть addons.");
            return;
        }

        const QString clientAddonPath = QDir(path).filePath(
            "addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua");
        const QString serverAddonPath = QDir(path).filePath(
            "addons/tfc_bridge/lua/autorun/server/tfc_bridge_server.lua");
        auto fileNeedsUpdate = [](const QString &resource, const QString &destination) {
            QFile embedded(resource);
            if (!embedded.open(QIODevice::ReadOnly)) return true;
            const QByteArray expected = embedded.readAll();
            QFile existing(destination);
            if (!existing.open(QIODevice::ReadOnly)) return true;
            return existing.readAll() != expected;
        };
        const bool needsWrite =
            fileNeedsUpdate(":/tfc_bridge.lua", clientAddonPath) ||
            fileNeedsUpdate(":/tfc_bridge_server.lua", serverAddonPath);

        if (needsWrite) {
            QString installError;
            if (!installBridgeFile(path, &installError)) {
                const QMessageBox::StandardButton answer = QMessageBox::question(
                    this, "Нет доступа к папке игры",
                    QString("Не удалось установить Lua bridge:\n%1\n\n"
                            "Повторить установку с запросом прав администратора Windows?")
                        .arg(installError),
                    QMessageBox::Yes | QMessageBox::No,
                    QMessageBox::Yes);
                if (answer != QMessageBox::Yes ||
                    !installBridgeElevated(path, &installError)) {
                    bridgeLabel_->setText("Lua bridge не установлен: " + installError);
                    QMessageBox::warning(
                        this, "Lua bridge не установлен", installError);
                    return;
                }
            }
        }

        QSettings settings("TFC", "TacticalFireControl");
        bridge_.setDeveloperOptions(
            developerConfig_.muzzleAttachment,
            developerConfig_.aimToleranceDeg,
            developerConfig_.aimStepDegPerTick);
        bridge_.setGmodDirectory(path);
        if (!bridge_.configured() || !bridge_.lastError().isEmpty()) {
            const QString error = bridge_.lastError().isEmpty()
                ? "Не удалось подключить каталог Garry's Mod DATA."
                : bridge_.lastError();
            bridgeLabel_->setText("Lua bridge не подключён: " + error);
            QMessageBox::warning(this, "Lua bridge не подключён", error);
            return;
        }
        settings.setValue("garrysmodDirectory", path);

        bridgeLabel_->setText(
            "Аддон TFC установлен. Ожидаем Garry's Mod и Lua-телеметрию…");
        if (showInstallMessage && needsWrite) {
            QMessageBox::information(
                this, "TFC — первичная установка",
                "Клиентский и серверный Lua bridge установлены в garrysmod/addons/tfc_bridge.\n\n"
                "Если Garry's Mod уже был запущен в момент установки — перезапусти его "
                "один раз. Для удалённого сервера серверный файл также нужно установить "
                "в addons/tfc_bridge на сервере.");
        }
        refreshBridgeStatus();
    }

    void chooseBsp()
    {
        const QString path = QFileDialog::getOpenFileName(
            this, "Открыть Source BSP v20", {}, "Source maps (*.bsp)");
        if (!path.isEmpty()) loadBsp(path);
    }

    void refreshBridgeStatus()
    {
        if (!bridge_.configured()) return;

        if (!bridge_.connected()) {
            bridgeLabel_->setText(
                "●  НЕТ ТЕЛЕМЕТРИИ  /  ЗАПУСТИ GARRY'S MOD");
            bridgeLabel_->setStyleSheet(
                "color:#b98d68;font-size:11px;font-weight:700;padding:6px 10px;border:1px solid #59422f;background:#15100c;");
            positionLabel_->setText("ОЖИДАНИЕ КООРДИНАТ ИЗ ИГРЫ");
            orientationLabel_->setText("—");
            muzzleLabel_->setText("—");
            weaponLabel_->setText("СЕРВЕРНАЯ ТЕЛЕМЕТРИЯ НЕДОСТУПНА");
            view3d_->setObserver(0, 0, 0, 0, 0, false);
            topdown_->setObserver(0, 0, 0, false);
            view3d_->setTrajectory({});
            topdown_->setTrajectory({});
            trajectoryLabel_->setText(targetSelected_
                ? "НЕТ ТЕЛЕМЕТРИИ ДЛЯ ПЕРЕСЧЁТА" : "Выбери точку цели");
            fireButton_->setEnabled(false);
            return;
        }

        const QJsonObject s = bridge_.state();
        const bool inVehicle = s.value("in_vehicle").toBool() &&
                               s.value("is_lvs").toBool();
        bridgeLabel_->setText(inVehicle
            ? "●  CONNECTED  /  LVS"
            : "●  CONNECTED  /  ОЖИДАНИЕ LVS");
        bridgeLabel_->setStyleSheet(inVehicle
            ? "color:#a4b578;font-size:11px;font-weight:700;padding:6px 10px;border:1px solid #465438;background:#10150d;"
            : "color:#c2a06e;font-size:11px;font-weight:700;padding:6px 10px;border:1px solid #59462d;background:#151109;");
        updateLiveOverlays(s);
        fireButton_->setEnabled(
            lastAimSet_ && inVehicle && s.value("fire_ready").toBool());
    }

    double effectiveProjectileSpeed(const QJsonObject &state, bool lvsCompensated) const
    {
        const double lvsSpeed = state.value("turret_ballistics_velocity").toDouble(0.0);
        if (lvsCompensated && lvsSpeed > 0.0)
            return qBound(1.0, lvsSpeed, 30000.0);
        if (developerConfig_.projectileSpeed > 0.0)
            return developerConfig_.projectileSpeed;
        return lvsSpeed > 0.0 ? qBound(1.0, lvsSpeed, 30000.0) : 4000.0;
    }

    double effectiveGravity(const QJsonObject &state, bool lvsCompensated) const
    {
        // LVS's built-in predictor always uses the live physenv gravity.
        if (!lvsCompensated && developerConfig_.gravityOverride > 0.0)
            return developerConfig_.gravityOverride;
        const double liveGravity = state.value("gravity").toDouble(600.0);
        return liveGravity > 0.0 ? liveGravity : 600.0;
    }

    std::optional<AimPlan> makeAimPlan(const QJsonObject &state) const
    {
        if (!targetSelected_ || !state.value("muzzle_valid").toBool())
            return std::nullopt;

        const QJsonObject muzzle = state.value("muzzle").toObject();
        const tfc::Vec3 origin{
            muzzle.value("x").toDouble(),
            muzzle.value("y").toDouble(),
            muzzle.value("z").toDouble()
        };
        const tfc::Vec3 target{targetX_, targetY_, targetZ_};
        const bool lvsCompensated = state.value("turret_ballistics_active").toBool();
        const double speed = effectiveProjectileSpeed(state, lvsCompensated);
        const double gravity = effectiveGravity(state, lvsCompensated);
        if (speed <= 0.0 || gravity <= 0.0)
            return std::nullopt;

        constexpr double degreesPerRadian = 180.0 / std::numbers::pi;
        const double dx = target.x - origin.x;
        const double dy = target.y - origin.y;
        const double dz = target.z - origin.z;
        const double horizontal = std::hypot(dx, dy);
        const double distance = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (horizontal < 1e-6 || distance < 1e-6)
            return std::nullopt;

        AimPlan plan;
        plan.projectileSpeed = speed;
        plan.gravity = gravity;
        plan.lvsCompensated = lvsCompensated;

        if (lvsCompensated) {
            // LVS CalcTurretAngles ray-traces from its sight attachment, then
            // compensates the real muzzle by gravity * (distance / velocity)^2.
            const QJsonObject sight = state.value("view_origin").toObject();
            const QJsonObject eye = state.value("eye_position").toObject();
            const QJsonObject &viewOrigin = sight.contains("x") ? sight : eye;
            const double sx = viewOrigin.value("x").toDouble(origin.x);
            const double sy = viewOrigin.value("y").toDouble(origin.y);
            const double sz = viewOrigin.value("z").toDouble(origin.z);
            const double viewDx = target.x - sx;
            const double viewDy = target.y - sy;
            const double viewDz = target.z - sz;
            const double viewHorizontal = std::hypot(viewDx, viewDy);
            if (viewHorizontal < 1e-6)
                return std::nullopt;

            plan.viewYawDeg = std::atan2(viewDy, viewDx) * degreesPerRadian;
            plan.viewPitchDeg =
                -std::atan2(viewDz, viewHorizontal) * degreesPerRadian;

            const double lvsTime = distance / speed;
            const double compensatedDz = dz + gravity * lvsTime * lvsTime;
            plan.muzzleYawDeg = std::atan2(dy, dx) * degreesPerRadian;
            plan.muzzlePitchDeg =
                -std::atan2(compensatedDz, horizontal) * degreesPerRadian;
            plan.elevationDeg = -plan.muzzlePitchDeg;
            plan.flightTimeSec = lvsTime;
            plan.gravityTerm = 1.0;
        } else {
            const auto ballistic = tfc::solveLowArc(origin, target, speed, gravity);
            if (!ballistic)
                return std::nullopt;
            plan.viewYawDeg = ballistic->worldYawDeg;
            plan.viewPitchDeg = ballistic->viewPitchDeg;
            plan.muzzleYawDeg = ballistic->worldYawDeg;
            plan.muzzlePitchDeg = ballistic->viewPitchDeg;
            plan.elevationDeg = ballistic->elevationDeg;
            plan.flightTimeSec = ballistic->flightTimeSec;
            plan.gravityTerm = 0.5;
        }

        return plan;
    }

    void updateTrajectory(const QJsonObject &state)
    {
        const auto plan = makeAimPlan(state);
        if (!plan) {
            view3d_->setTrajectory({});
            topdown_->setTrajectory({});
            trajectoryLabel_->setText(targetSelected_
                ? "НЕТ БАЛЛИСТИЧЕСКОГО РЕШЕНИЯ / НЕТ ДАННЫХ ДУЛА"
                : "Выбери точку цели");
            return;
        }

        constexpr double radiansPerDegree = std::numbers::pi / 180.0;
        const double yaw = plan->muzzleYawDeg * radiansPerDegree;
        const double elevation = plan->elevationDeg * radiansPerDegree;
        const double speed = plan->projectileSpeed;
        const double vx = speed * std::cos(elevation) * std::cos(yaw);
        const double vy = speed * std::cos(elevation) * std::sin(yaw);
        const double vz = speed * std::sin(elevation);

        const QJsonObject muzzle = state.value("muzzle").toObject();
        const tfc::Vec3 origin{
            muzzle.value("x").toDouble(),
            muzzle.value("y").toDouble(),
            muzzle.value("z").toDouble()
        };
        QVector<tfc::Vec3> points;
        constexpr int samples = 44;
        points.reserve(samples + 1);
        for (int i = 0; i <= samples; ++i) {
            const double t = plan->flightTimeSec *
                static_cast<double>(i) / static_cast<double>(samples);
            points.push_back({
                origin.x + vx * t,
                origin.y + vy * t,
                origin.z + vz * t - plan->gravityTerm * plan->gravity * t * t
            });
        }
        view3d_->setTrajectory(points);
        topdown_->setTrajectory(points);
        trajectoryLabel_->setText(QString(
            "%1  ·  %2 с\nУгол ствола +%3°  ·  скорость %4\nГравитация %5")
            .arg(plan->lvsCompensated ? "LVS AUTO-COMPENSATION" : "TFC LOW ARC")
            .arg(plan->flightTimeSec, 0, 'f', 2)
            .arg(plan->elevationDeg, 0, 'f', 2)
            .arg(speed, 0, 'f', 0)
            .arg(plan->gravity, 0, 'f', 1));
    }

    void updateLiveOverlays(const QJsonObject &state)
    {
        const bool inVehicle = state.value("in_vehicle").toBool() &&
                               state.value("is_lvs").toBool();
        const bool currentLvsMode = state.value("turret_ballistics_active").toBool();
        if (lastAimSet_ && currentLvsMode != lastAimLvsCompensated_) {
            lastAimSet_ = false;
            fireButton_->setEnabled(false);
            setStatus("Режим баллистики LVS изменился. Повтори F7 для нового режима.");
        }
        const QJsonObject player = state.value("player_position").toObject();
        const QJsonObject vehicle = state.value("vehicle_position").toObject();
        const QJsonObject position = inVehicle ? vehicle : player;
        const bool positionValid = state.contains("player_position") &&
            position.contains("x") && position.contains("y") && position.contains("z");
        const double x = position.value("x").toDouble();
        const double y = position.value("y").toDouble();
        const double z = position.value("z").toDouble();
        const QJsonObject eye = state.value("eye_angles").toObject();
        const QJsonObject aim = state.value("player_aim_angles").toObject();
        const double eyeYaw = eye.value("yaw").toDouble();
        const double eyePitch = eye.value("pitch").toDouble();
        const double aimYaw = aim.contains("yaw") ? aim.value("yaw").toDouble() : eyeYaw;
        const double aimPitch = aim.contains("pitch") ? aim.value("pitch").toDouble() : eyePitch;

        // LVS gunner aim-vector is the actual direction fed into turret control.
        view3d_->setObserver(x, y, z, aimYaw, aimPitch, positionValid);
        topdown_->setObserver(x, y, aimYaw, positionValid);

        if (positionValid) {
            if (inVehicle) {
                positionLabel_->setText(QString(
                    "ПЛАТФОРМА  X %1  Y %2  Z %3\nНАВОДЧИК  X %4  Y %5  Z %6")
                    .arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1)
                    .arg(player.value("x").toDouble(), 0, 'f', 1)
                    .arg(player.value("y").toDouble(), 0, 'f', 1)
                    .arg(player.value("z").toDouble(), 0, 'f', 1));
            } else {
                positionLabel_->setText(QString("ИГРОК  X %1  Y %2  Z %3")
                    .arg(x, 0, 'f', 1).arg(y, 0, 'f', 1).arg(z, 0, 'f', 1));
            }
        } else {
            positionLabel_->setText("ОЖИДАНИЕ КООРДИНАТ");
        }

        if (state.contains("eye_angles")) {
            orientationLabel_->setText(QString(
                "EYE %1° / %2°\nLVS AIM %3° / %4°\nTURRET %5° / %6°\nBC %7 · ZOOM %8 · 3P %9")
                .arg(eyeYaw, 0, 'f', 1).arg(eyePitch, 0, 'f', 1)
                .arg(aimYaw, 0, 'f', 1).arg(aimPitch, 0, 'f', 1)
                .arg(state.value("turret_yaw").toDouble(), 0, 'f', 1)
                .arg(state.value("turret_pitch").toDouble(), 0, 'f', 1)
                .arg(state.value("turret_ballistics_active").toBool() ? "ON" : "OFF")
                .arg(state.value("zoom_held").toBool() ? "ON" : "OFF")
                .arg(state.value("third_person_mode").toBool() ? "ON" : "OFF"));
        } else {
            orientationLabel_->setText("—");
        }

        const QJsonObject muzzle = state.value("muzzle").toObject();
        const QJsonObject muzzleAngle = state.value("muzzle_angle").toObject();
        if (state.value("muzzle_valid").toBool()) {
            muzzleLabel_->setText(QString("%1\nX %2  Y %3  Z %4\nYAW %5°  PITCH %6°  ERR %7°")
                .arg(state.value("muzzle_attachment").toString())
                .arg(muzzle.value("x").toDouble(), 0, 'f', 1)
                .arg(muzzle.value("y").toDouble(), 0, 'f', 1)
                .arg(muzzle.value("z").toDouble(), 0, 'f', 1)
                .arg(muzzleAngle.value("yaw").toDouble(), 0, 'f', 1)
                .arg(muzzleAngle.value("pitch").toDouble(), 0, 'f', 1)
                .arg(state.value("muzzle_error_deg").toDouble(), 0, 'f', 2));
        } else {
            muzzleLabel_->setText("MUZZLE ATTACHMENT NOT FOUND");
        }

        const bool gunner = state.value("gunner_present").toBool();
        const bool overheated = state.value("overheated").toBool();
        const bool limitedAmmo = state.value("ammo_limited").toBool();
        const double clip = state.value("ammo_clip").toDouble(-1.0);
        const double ammo = state.value("ammo_reserve").toDouble(-1.0);
        QString ammoText = limitedAmmo
            ? QString("БОЕКОМПЛЕКТ %1").arg(ammo, 0, 'f', 0)
            : QString("БОЕКОМПЛЕКТ ∞");
        const QString clipText = state.value("clip_relevant").toBool()
            ? QString("CLIP/HEAT %1").arg(clip, 0, 'f', 0)
            : QString("CLIP/HEAT ∞");
        QString readiness = !inVehicle ? "НЕ В LVS" :
            (!gunner ? "НЕТ АКТИВНОГО ОРУЖИЯ" :
             (overheated ? "ПЕРЕГРЕВ" :
              (!state.value("gunner_ready").toBool()
                ? state.value("weapon_reason").toString()
                : (!lastAimSet_ ? "СНАЧАЛА F7 — РАСЧИТАТЬ НАВЕДЕНИЕ" :
                   (state.value("aim_aligned").toBool()
                    ? "СТВОЛ В ДОПУСКЕ" : "ОЖИДАНИЕ СОВМЕЩЕНИЯ СТВОЛА")))));
        weaponLabel_->setText(QString("%1\n%2  ·  %3\n%4")
            .arg(readiness).arg(ammoText).arg(clipText)
            .arg(state.value("server_fire_message").toString()));
        updateTrajectory(state);
    }

    void aim()
    {
        if (!bsp_.isLoaded()) {
            setStatus("F7: сначала загрузи карту BSP.");
            return;
        }
        if (!targetSelected_) {
            setStatus("F7: сначала выбери точку кликом по 3D-карте.");
            return;
        }
        if (!bridge_.connected()) {
            setStatus("F7: нет Lua-телеметрии. Сначала запусти Garry's Mod.");
            return;
        }

        const QJsonObject state = bridge_.state();
        if (!state.value("in_vehicle").toBool() || !state.value("is_lvs").toBool()) {
            setStatus("F7: нужно занять место наводчика в LVS-машине.");
            return;
        }
        if (!state.value("muzzle_valid").toBool()) {
            setStatus("F7: не найден muzzle attachment в Garry's Mod.");
            return;
        }

        const auto plan = makeAimPlan(state);
        if (!plan) {
            setStatus("F7: нет решения. Проверь скорость снаряда, гравитацию и дальность.");
            return;
        }

        if (!bridge_.sendAim(
                plan->viewYawDeg, plan->viewPitchDeg,
                plan->muzzleYawDeg, plan->muzzlePitchDeg)) {
            setStatus("F7: не удалось записать команду Lua bridge: " + bridge_.lastError());
            return;
        }

        lastAimYaw_ = plan->viewYawDeg;
        lastAimPitch_ = plan->viewPitchDeg;
        lastAimMuzzleYaw_ = plan->muzzleYawDeg;
        lastAimMuzzlePitch_ = plan->muzzlePitchDeg;
        lastAimLvsCompensated_ = plan->lvsCompensated;
        lastAimSet_ = true;
        fireButton_->setEnabled(state.value("fire_ready").toBool());
        setStatus(QString("F7: %1 · view %2°/%3° · muzzle %4°/%5°")
            .arg(plan->lvsCompensated ? "LVS BC" : "TFC LOW ARC")
            .arg(plan->viewYawDeg, 0, 'f', 1)
            .arg(plan->viewPitchDeg, 0, 'f', 1)
            .arg(plan->muzzleYawDeg, 0, 'f', 1)
            .arg(plan->muzzlePitchDeg, 0, 'f', 1));
    }

    void fire()
    {
        if (!lastAimSet_) {
            setStatus("F8 заблокирована: сначала выбери цель и выполни наведение F7.");
            return;
        }
        if (!bridge_.connected()) {
            setStatus("F8: Lua bridge не подключён.");
            return;
        }
        if (!bridge_.state().value("fire_ready").toBool()) {
            setStatus(
                "F8 заблокирована: LVS ещё не подтвердил наведение и готовность орудия.");
            return;
        }
        if (!bridge_.sendFire(
                lastAimYaw_, lastAimPitch_,
                lastAimMuzzleYaw_, lastAimMuzzlePitch_)) {
            setStatus("F8: не удалось записать команду: " + bridge_.lastError());
            return;
        }
        setStatus("F8: серверу отправлен запрос на один импульс LVS ATTACK.");
    }

    void setTarget(double x, double y, double z)
    {
        targetX_ = x;
        targetY_ = y;
        targetZ_ = z;
        targetSelected_ = true;
        lastAimSet_ = false;
        fireButton_->setEnabled(false);
        view3d_->setTarget(x, y, z);
        topdown_->setMarker(x, y);
        targetLabel_->setText(QString("ЦЕЛЬ  X %1  Y %2\nZ %3")
            .arg(x, 0, 'f', 1)
            .arg(y, 0, 'f', 1)
            .arg(z, 0, 'f', 1));
        updateTrajectory(bridge_.connected() ? bridge_.state() : QJsonObject{});
    }

    void setStatus(const QString &message)
    {
        statusBar()->showMessage(message, 8000);
    }

    QTabWidget *tabs_ = nullptr;
    bspvision::Bsp3DView *view3d_ = nullptr;
    TopDownPicker *topdown_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *targetLabel_ = nullptr;
    QLabel *bridgeLabel_ = nullptr;
    QLabel *positionLabel_ = nullptr;
    QLabel *orientationLabel_ = nullptr;
    QLabel *muzzleLabel_ = nullptr;
    QLabel *weaponLabel_ = nullptr;
    QLabel *trajectoryLabel_ = nullptr;
    DeveloperConfig developerConfig_;
    QPushButton *aimButton_ = nullptr;
    QPushButton *fireButton_ = nullptr;
    bspvision::BspFile bsp_;
    FileBridge bridge_;
    double targetX_ = 0.0;
    double targetY_ = 0.0;
    double targetZ_ = 0.0;
    double lastAimYaw_ = 0.0;
    double lastAimPitch_ = 0.0;
    double lastAimMuzzleYaw_ = 0.0;
    double lastAimMuzzlePitch_ = 0.0;
    bool targetSelected_ = false;
    bool lastAimSet_ = false;
    bool lastAimLvsCompensated_ = false;
    bool hotkeyF7Registered_ = false;
    bool hotkeyF8Registered_ = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("TFC");
    QApplication::setOrganizationName("TFC");

    const QStringList args = QApplication::arguments();
    if (args.size() == 3 && args.at(1) == "--install-bridge") {
        QString error;
        if (!installBridgeFile(args.at(2), &error)) {
            QMessageBox::critical(
                nullptr, "Не удалось установить Lua bridge", error);
            return 1;
        }
        return 0;
    }

    MainWindow window;
    window.show();

    if (args.size() > 1) {
        window.loadBsp(args.at(1));
    } else {
        QTimer::singleShot(0, &window, [&window] {
            QMessageBox::information(
                &window, "TFC — краткая инструкция",
                "Перетащи .bsp на окно или выбери файл кнопкой «Открыть BSP».\n"
                "Кликни по поверхности карты в 3D, чтобы выбрать цель.\n"
                "В Garry's Mod займи место наводчика LVS; F7 наведёт на точку, "
                "F8 отправит одиночную команду ATTACK.\n"
                "Окно карты остаётся поверх игры.");
        });
    }

    return app.exec();
}
