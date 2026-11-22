#include "app_language.hpp"
#include "ballistics.hpp"
#include "bsp_3d_view.hpp"
#include "bsp_file.hpp"
#include "file_bridge.hpp"
#include "topdown_picker.hpp"

#include <QApplication>
#include <QCoreApplication>
#include <QByteArray>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QJsonObject>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QMimeData>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QSettings>
#include <QStatusBar>
#include <QTabWidget>
#include <QVBoxLayout>

#include <utility>

#ifdef Q_OS_WIN
#define NOMINMAX
#include <windows.h>
#endif

struct DeveloperConfig {
    double projectileSpeed = 4000.0;
    double gravity = 600.0;
    double aimToleranceDeg = 1.5;
    double aimStepDegPerTick = 2.0;
    QString muzzleAttachment = QStringLiteral("auto");
    QString path;
};

class MainWindow final : public QMainWindow {
public:
    MainWindow()
    {
        developerConfig_ = loadDeveloperConfig();
        setWindowTitle("TFC — Tactical Fire Control");
        resize(1120, 760);
        setAcceptDrops(true);
        setStyleSheet(
            "QMainWindow { background:#20252c; color:#e6ebf2; }"
            "QWidget { color:#e6ebf2; }"
            "QPushButton { background:#343d48; border:1px solid #4b5664; padding:8px; }"
            "QPushButton:hover { background:#414d5b; }"
            "QPushButton:disabled { color:#6e7784; background:#292f37; }"
            "QDoubleSpinBox { background:#171b21; border:1px solid #4b5664; padding:5px; }"
            "QTabWidget::pane { border:1px solid #414a56; }"
            "QTabBar::tab { background:#2a3038; padding:8px 14px; }"
            "QTabBar::tab:selected { background:#414c59; }"
        );

        auto *root = new QWidget(this);
        auto *layout = new QHBoxLayout(root);
        layout->setContentsMargins(8, 8, 8, 8);
        layout->setSpacing(8);

        tabs_ = new QTabWidget(root);
        view3d_ = new bspvision::Bsp3DView(tabs_);
        topdown_ = new TopDownPicker(tabs_);
        tabs_->addTab(view3d_, "3D map");
        tabs_->addTab(topdown_, "Top-down / выбор цели");
        layout->addWidget(tabs_, 1);

        auto *side = new QWidget(root);
        side->setMinimumWidth(290);
        side->setMaximumWidth(340);
        auto *right = new QVBoxLayout(side);
        right->setContentsMargins(8, 4, 4, 4);
        right->setSpacing(7);

        auto *brand = new QLabel(
            "<b style='font-size:20px'>TFC</b><br/>TACTICAL FIRE CONTROL"
            "<br/><span style='color:#9aa7b8'>BSP planner · Garry's Mod bridge</span>", side);
        brand->setTextFormat(Qt::RichText);
        right->addWidget(brand);

        auto *openButton = new QPushButton("Открыть BSP…", side);
        auto *gameButton = new QPushButton("Путь к Garry's Mod…", side);
        right->addWidget(openButton);
        right->addWidget(gameButton);

        mapLabel_ = new QLabel(
            "Карта не загружена.\nПеретащи .bsp на окно или выбери файл.", side);
        mapLabel_->setWordWrap(true);
        right->addWidget(mapLabel_);

        right->addWidget(new QLabel("<b>Цель (Source units)</b>", side));
        targetX_ = makeSpin(side, -32768.0, 32768.0, 100.0);
        targetY_ = makeSpin(side, -32768.0, 32768.0, 100.0);
        targetZ_ = makeSpin(side, -32768.0, 32768.0, 0.0);
        addField(right, "X", targetX_);
        addField(right, "Y", targetY_);
        addField(right, "Z", targetZ_);

        auto *zHint = new QLabel(
            "Высоту Z проверь вручную: на карте могут быть несколько уровней.", side);
        zHint->setWordWrap(true);
        zHint->setStyleSheet("color:#a8b0bd;");
        right->addWidget(zHint);

        right->addWidget(new QLabel("<b>Баллистический профиль</b>", side));
        auto *profileHint = new QLabel(
            QString("Developer CFG загружен · %1\n%2 ед/с · гравитация %3")
                .arg(QFileInfo(developerConfig_.path).fileName())
                .arg(developerConfig_.projectileSpeed, 0, 'f', 0)
                .arg(developerConfig_.gravity, 0, 'f', 0), side);
        profileHint->setWordWrap(true);
        profileHint->setStyleSheet("color:#c5a879;");
        profileHint->setToolTip(
            "Для изменения параметров закрой TFC и отредактируй tfc.cfg рядом с EXE.");
        right->addWidget(profileHint);

        right->addWidget(new QLabel("<b>Lua bridge</b>", side));
        bridgeLabel_ = new QLabel("Не настроен", side);
        bridgeLabel_->setWordWrap(true);
        right->addWidget(bridgeLabel_);

        telemetryLabel_ = new QLabel("Ожидание телеметрии игры…", side);
        telemetryLabel_->setWordWrap(true);
        telemetryLabel_->setStyleSheet("color:#aeb9c8;");
        right->addWidget(telemetryLabel_);

        aimButton_ = new QPushButton("F7 — НАВЕСТИ", side);
        fireButton_ = new QPushButton("F8 — ОДИН ВЫСТРЕЛ", side);
        cancelButton_ = new QPushButton("Отменить наведение", side);
        right->addWidget(aimButton_);
        right->addWidget(fireButton_);
        right->addWidget(cancelButton_);
        right->addStretch(1);

        auto *hotkeyInfo = new QLabel(
            "Глобальные клавиши: F7 — наведение, F8 — одиночный импульс ATTACK. "
            "Сначала запусти Garry's Mod, затем TFC.", side);
        hotkeyInfo->setWordWrap(true);
        hotkeyInfo->setStyleSheet("color:#a8b0bd;");
        right->addWidget(hotkeyInfo);

        layout->addWidget(side);
        setCentralWidget(root);

        connect(openButton, &QPushButton::clicked, this, [this] { chooseBsp(); });
        connect(gameButton, &QPushButton::clicked, this, [this] { chooseGameDirectory(); });
        connect(aimButton_, &QPushButton::clicked, this, [this] { aim(); });
        connect(fireButton_, &QPushButton::clicked, this, [this] { fire(); });
        connect(cancelButton_, &QPushButton::clicked, this, [this] {
            bridge_.sendCancel();
            setStatus("Наведение отменено.");
        });

        topdown_->setPointCallback([this](double x, double y) {
            targetX_->setValue(x);
            targetY_->setValue(y);
            topdown_->setMarker(x, y);
        });

        bridge_.setUpdatedCallback([this] { refreshBridgeStatus(); });

        QSettings settings("TFC", "TacticalFireControl");
        QString gmod = settings.value("garrysmodDirectory").toString();
        if (gmod.isEmpty() || !QDir(gmod).exists("addons"))
            gmod = detectGmodDirectory();

        if (gmod.isEmpty()) {
            bridgeLabel_->setText("Первичная настройка: укажи папку garrysmod.");
            statusBar()->showMessage(
                "Сначала запусти Garry's Mod, затем настрой путь к папке garrysmod.");
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
        topdown_->setPolygons(polygons);
        mapLabel_->setText(QString("%1\n%2 faces · %3 vertices · %4 displacement surfaces")
            .arg(QFileInfo(path).fileName())
            .arg(bsp_.faces().size())
            .arg(bsp_.vertices().size())
            .arg(bsp_.displacementCount()));
        statusBar()->showMessage("Карта загружена: " + path);
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
                    "# TFC developer-only ballistics settings. Restart TFC after editing.\n"
                    "[ballistics]\n"
                    "projectile_speed=4000\n"
                    "gravity=600\n\n"
                    "[aim]\n"
                    "tolerance_deg=1.5\n"
                    "step_deg_per_tick=2.0\n\n"
                    "[game]\n"
                    "muzzle_attachment=auto\n";
                file.write(defaults);
            }
        }

        QSettings cfg(config.path, QSettings::IniFormat);
        config.projectileSpeed = qBound(
            1.0, cfg.value("ballistics/projectile_speed", 4000.0).toDouble(), 30000.0);
        config.gravity = qBound(
            1.0, cfg.value("ballistics/gravity", 600.0).toDouble(), 3000.0);
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

    static QDoubleSpinBox *makeSpin(
        QWidget *parent, double minimum, double maximum, double value)
    {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setRange(minimum, maximum);
        spin->setDecimals(2);
        spin->setSingleStep(16.0);
        spin->setValue(value);
        return spin;
    }

    static void addField(
        QVBoxLayout *layout, const QString &label, QDoubleSpinBox *spin)
    {
        auto *row = new QHBoxLayout();
        row->addWidget(new QLabel(label));
        row->addWidget(spin, 1);
        layout->addLayout(row);
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
        if (existing.open(QIODevice::ReadOnly) && existing.readAll() == addonBytes)
            needsWrite = false;

        if (needsWrite) {
            if (!QDir().mkpath(QFileInfo(addonPath).absolutePath())) {
                bridgeLabel_->setText("Не удалось создать каталог аддона.");
                return;
            }
            QSaveFile target(addonPath);
            if (!target.open(QIODevice::WriteOnly) ||
                target.write(addonBytes) != addonBytes.size() ||
                !target.commit()) {
                bridgeLabel_->setText(
                    "Не удалось установить Lua bridge: " + target.errorString());
                return;
            }
        }

        QSettings settings("TFC", "TacticalFireControl");
        settings.setValue("garrysmodDirectory", path);
        bridge_.setDeveloperOptions(
            developerConfig_.muzzleAttachment,
            developerConfig_.aimToleranceDeg,
            developerConfig_.aimStepDegPerTick);
        bridge_.setGmodDirectory(path);

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
            telemetryLabel_->setText("Ожидание state.json…");
            fireButton_->setEnabled(false);
            return;
        }

        const QJsonObject s = bridge_.state();
        bridgeLabel_->setText("Lua bridge: CONNECTED · protocol 1");
        const QString vehicleClass = s.value("vehicle_class").toString("нет LVS-машины");
        const QJsonObject muzzleAngle = s.value("muzzle_angle").toObject();
        telemetryLabel_->setText(QString(
            "Машина: %1\nLVS-сиденье: %2\nAttachment ствола: %3\n"
            "Фактический угол ствола: yaw %4° / pitch %5°\n"
            "Орудие в допуске: %6\nОрудие готово: %7\n"
            "Боезапас / перегрев: %8 / %9")
            .arg(vehicleClass)
            .arg(s.value("in_vehicle").toBool() ? "да" : "нет")
            .arg(s.value("muzzle_attachment").toString("не найден"))
            .arg(muzzleAngle.value("yaw").toDouble(), 0, 'f', 2)
            .arg(muzzleAngle.value("pitch").toDouble(), 0, 'f', 2)
            .arg(s.value("aim_aligned").toBool() ? "да" : "нет")
            .arg(s.value("gunner_ready").toBool() ? "да" : "нет")
            .arg(s.value("ammo_clip").toInt())
            .arg(s.value("overheated").toBool() ? "перегрев" : "норма"));

        // Keep the user's ballistic profile editable; live bridge values are telemetry,
        // not an instruction to overwrite custom speed/gravity settings every poll.
        fireButton_->setEnabled(s.value("fire_ready").toBool());
    }

    void aim()
    {
        if (!bsp_.isLoaded()) {
            setStatus("F7: сначала загрузи карту BSP.");
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
            setStatus("F7: attachment ствола не найден. Нужна настройка профиля BM-30.");
            return;
        }

        const QJsonObject muzzle = s.value("muzzle").toObject();
        const tfc::Vec3 origin{
            muzzle.value("x").toDouble(),
            muzzle.value("y").toDouble(),
            muzzle.value("z").toDouble()
        };
        const tfc::Vec3 target{
            targetX_->value(), targetY_->value(), targetZ_->value()
        };

        const auto solution = tfc::solveLowArc(
            origin, target, developerConfig_.projectileSpeed, developerConfig_.gravity);
        if (!solution) {
            setStatus("F7: для выбранной точки нет низкой баллистической траектории.");
            return;
        }

        if (!bridge_.sendAim(
                solution->worldYawDeg, solution->viewPitchDeg)) {
            setStatus("F7: не удалось записать команду Lua bridge: " + bridge_.lastError());
            return;
        }

        lastAimYaw_ = solution->worldYawDeg;
        lastAimPitch_ = solution->viewPitchDeg;
        setStatus(QString(
            "F7: команда наведения отправлена · yaw %1° · pitch %2° · полёт %3 с")
            .arg(solution->worldYawDeg, 0, 'f', 2)
            .arg(solution->viewPitchDeg, 0, 'f', 2)
            .arg(solution->flightTimeSec, 0, 'f', 2));
    }

    void fire()
    {
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

    void setStatus(const QString &message)
    {
        statusBar()->showMessage(message, 8000);
    }

    QTabWidget *tabs_ = nullptr;
    bspvision::Bsp3DView *view3d_ = nullptr;
    TopDownPicker *topdown_ = nullptr;
    QLabel *mapLabel_ = nullptr;
    QLabel *bridgeLabel_ = nullptr;
    QLabel *telemetryLabel_ = nullptr;
    QDoubleSpinBox *targetX_ = nullptr;
    QDoubleSpinBox *targetY_ = nullptr;
    QDoubleSpinBox *targetZ_ = nullptr;
    DeveloperConfig developerConfig_;
    QPushButton *aimButton_ = nullptr;
    QPushButton *fireButton_ = nullptr;
    bspvision::BspFile bsp_;
    FileBridge bridge_;
    double lastAimYaw_ = 0.0;
    double lastAimPitch_ = 0.0;
    bool hotkeyF7Registered_ = false;
    bool hotkeyF8Registered_ = false;
};

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QApplication::setApplicationName("TFC");
    QApplication::setOrganizationName("TFC");

    MainWindow window;
    window.show();

    const QStringList args = QApplication::arguments();
    if (args.size() > 1)
        window.loadBsp(args.at(1));

    return app.exec();
}
