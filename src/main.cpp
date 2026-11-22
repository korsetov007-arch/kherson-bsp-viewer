#include "app_language.hpp"
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
#include <string>
#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#include <shellapi.h>
#endif

struct DeveloperConfig {
    double aimToleranceDeg = 1.5;
    double aimStepDegPerTick = 2.0;
    QString muzzleAttachment = QStringLiteral("auto");
    QString path;
};

bool installBridgeFile(const QString &garrysmodDirectory, QString *error)
{
    if (!QDir(garrysmodDirectory).exists("addons")) {
        if (error) *error = "В выбранной папке нет каталога addons.";
        return false;
    }

    QFile embedded(":/tfc_bridge.lua");
    if (!embedded.open(QIODevice::ReadOnly)) {
        if (error) *error = "Не удалось прочитать встроенный Lua bridge.";
        return false;
    }
    const QByteArray addonBytes = embedded.readAll();
    const QString addonPath = QDir(garrysmodDirectory).filePath(
        "addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua");
    if (!QDir().mkpath(QFileInfo(addonPath).absolutePath())) {
        if (error) *error = "Windows не разрешила создать папку аддона.";
        return false;
    }

    QSaveFile target(addonPath);
    if (!target.open(QIODevice::WriteOnly)) {
        if (error) *error = target.errorString();
        return false;
    }
    if (target.write(addonBytes) != addonBytes.size()) {
        if (error) *error = target.errorString();
        target.cancelWriting();
        return false;
    }
    if (!target.commit()) {
        if (error) *error = target.errorString();
        return false;
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
        resize(1180, 780);
        setWindowFlag(Qt::WindowStaysOnTopHint);
        setAcceptDrops(true);
        setStyleSheet(
            "QMainWindow { background:#161616; color:#e8e8e8; }"
            "QWidget { color:#e8e8e8; }"
            "QPushButton { background:#232323; border:1px solid #333333; padding:6px 14px; border-radius:4px; }"
            "QPushButton:hover { background:#2d2d2d; border-color:#444444; }"
            "QPushButton:pressed { background:#1a1a1a; }"
            "QPushButton:disabled { color:#555555; background:#1c1c1c; border-color:#2a2a2a; }"
            "QMenuBar { background:#161616; }"
            "QTabWidget::pane { border:none; }"
            "QTabBar::tab { background:#232323; padding:8px 16px; margin-right:2px; }"
            "QTabBar::tab:selected { background:#161616; }"
            "QLabel { color:#b0b0b0; }"
        );
        auto *settingsMenu = menuBar()->addMenu("Настройки");
        QAction *gameDirectoryAction =
            settingsMenu->addAction("Папка Garry's Mod…");
        connect(gameDirectoryAction, &QAction::triggered, this, [this] {
            chooseGameDirectory();
        });

        auto *root = new QWidget(this);
        auto *layout = new QHBoxLayout(root);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);

        tabs_ = new QTabWidget(root);
        view3d_ = new bspvision::Bsp3DView(tabs_);
        topdown_ = new TopDownPicker(tabs_);
        tabs_->addTab(view3d_, "3D");
        tabs_->addTab(topdown_, "Топ-даун X/Y");
        layout->addWidget(tabs_, 1);

        auto *side = new QWidget(root);
        side->setMinimumWidth(150);
        side->setMaximumWidth(170);
        auto *right = new QVBoxLayout(side);
        right->setContentsMargins(8, 4, 4, 4);
        right->setSpacing(7);

        auto *openButton = new QPushButton("Открыть BSP…", side);
        right->addWidget(openButton);

        mapLabel_ = new QLabel("Карта не загружена", side);
        mapLabel_->setWordWrap(true);
        right->addWidget(mapLabel_);

        targetLabel_ = new QLabel("Цель не выбрана", side);
        targetLabel_->setWordWrap(true);
        right->addWidget(targetLabel_);

        right->addWidget(new QLabel("Мост GMod", side));
        bridgeLabel_ = new QLabel("Не настроен", side);
        bridgeLabel_->setWordWrap(true);
        right->addWidget(bridgeLabel_);

        aimButton_ = new QPushButton("F7 — Навести", side);
        fireButton_ = new QPushButton("F8 — Огонь", side);
        fireButton_->setEnabled(false);
        right->addWidget(aimButton_);
        right->addWidget(fireButton_);
        right->addStretch(1);

        layout->addWidget(side);
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
        topdown_->setPolygons(polygons);
        targetSelected_ = false;
        lastAimSet_ = false;
        targetLabel_->setText("Цель не выбрана.\nКликни по поверхности в 3D-карте.");
        mapLabel_->setText(QFileInfo(path).fileName());
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
                    "[aim]\n"
                    "tolerance_deg=1.5\n"
                    "step_deg_per_tick=2.0\n\n"
                    "[game]\n"
                    "muzzle_attachment=auto\n";
                file.write(defaults);
            }
        }

        QSettings cfg(config.path, QSettings::IniFormat);
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

        const QString addonPath = QDir(path).filePath(
            "addons/tfc_bridge/lua/autorun/client/tfc_bridge.lua");
        QFile embedded(":/tfc_bridge.lua");
        if (!embedded.open(QIODevice::ReadOnly)) {
            bridgeLabel_->setText("Не удалось прочитать встроенный Lua bridge.");
            return;
        }
        const QByteArray addonBytes = embedded.readAll();

        bool needsWrite = true;
        QFile existing(addonPath);
        if (existing.open(QIODevice::ReadOnly)) {
            needsWrite = existing.readAll() != addonBytes;
            existing.close();
        }

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
                "Lua bridge установлен в garrysmod/addons/tfc_bridge.\n\n"
                "Если Garry's Mod уже был запущен в момент установки — перезапусти его "
                "один раз. В дальнейшем достаточно запускать игру первой, затем TFC.");
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
                "Нет телеметрии. Запусти Garry's Mod — Lua bridge подключится сам.");
            fireButton_->setEnabled(false);
            return;
        }

        const QJsonObject s = bridge_.state();
        bridgeLabel_->setText(
            s.value("in_vehicle").toBool() && s.value("is_lvs").toBool()
                ? "Lua bridge подключён · LVS"
                : "Lua bridge подключён · займи место в LVS");
        fireButton_->setEnabled(
            lastAimSet_ && s.value("fire_ready").toBool());
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

        const QJsonObject s = bridge_.state();
        if (!s.value("in_vehicle").toBool() || !s.value("is_lvs").toBool()) {
            setStatus("F7: нужно занять место наводчика в LVS-машине.");
            return;
        }
        if (!s.value("muzzle_valid").toBool()) {
            setStatus("F7: не найден muzzle attachment в Garry's Mod.");
            return;
        }

        const QJsonObject muzzle = s.value("muzzle").toObject();
        const double dx = targetX_ - muzzle.value("x").toDouble();
        const double dy = targetY_ - muzzle.value("y").toDouble();
        const double dz = targetZ_ - muzzle.value("z").toDouble();
        const double horizontalDistance = std::hypot(dx, dy);
        if (horizontalDistance < 1e-6 && std::abs(dz) < 1e-6) {
            setStatus("F7: цель совпадает с положением ствола.");
            return;
        }
        constexpr double degreesPerRadian = 180.0 / std::numbers::pi;
        const double worldYaw = std::atan2(dy, dx) * degreesPerRadian;
        const double viewPitch =
            -std::atan2(dz, horizontalDistance) * degreesPerRadian;

        if (!bridge_.sendAim(worldYaw, viewPitch)) {
            setStatus("F7: не удалось записать команду Lua bridge: " + bridge_.lastError());
            return;
        }

        lastAimYaw_ = worldYaw;
        lastAimPitch_ = viewPitch;
        lastAimSet_ = true;
        setStatus(QString(
            "F7: наведение на точку · yaw %1° · pitch %2°")
            .arg(worldYaw, 0, 'f', 2)
            .arg(viewPitch, 0, 'f', 2));
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
        if (!bridge_.sendFire(lastAimYaw_, lastAimPitch_)) {
            setStatus("F8: не удалось записать команду: " + bridge_.lastError());
            return;
        }
        setStatus("F8: одиночная команда ATTACK передана Lua bridge.");
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
        targetLabel_->setText(QString("Цель · X %1 · Y %2 · Z %3")
            .arg(x, 0, 'f', 1)
            .arg(y, 0, 'f', 1)
            .arg(z, 0, 'f', 1));
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
    bool targetSelected_ = false;
    bool lastAimSet_ = false;
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
