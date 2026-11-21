#include "app_language.hpp"
#include "bsp_file.hpp"
#include "bsp_map_view.hpp"
#include "bsp_3d_view.hpp"
#include "ballistics_view.hpp"
#include "live_overlay.hpp"
#include "cl_showpos_reader.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QHash>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QSettings>
#include <QApplication>
#include <QFileDialog>
#include <QGuiApplication>
#include <QList>
#include <QScreen>
#include <QTimer>
#include <QLocalServer>
#include <QLocalSocket>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QDateTime>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QStatusBar>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <QVector3D>
#include <QtMath>


class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        resize(1360, 900);
        QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
        const QString defaultLanguage =
            QLocale::system().language() == QLocale::Russian ? QStringLiteral("ru") : QStringLiteral("en");
        const QString savedLanguage = settings.value(QStringLiteral("language"), defaultLanguage).toString();
        bspvision::setUiLanguage(savedLanguage == QStringLiteral("ru")
            ? bspvision::UiLanguage::Russian : bspvision::UiLanguage::English);
        screenCaptureTimer_ = new QTimer(this);
        screenCaptureTimer_->setInterval(33);
        connect(screenCaptureTimer_, &QTimer::timeout, this, [this] { pollClShowPosReader(); });
        createSettingsMenu();
        rebuildInterface();
        overlay_ = new bspvision::LiveOverlay();
        bridgeServer_ = new QLocalServer(this);
        QLocalServer::removeServer(QStringLiteral("SBGLBridge"));
        bridgeServer_->listen(QStringLiteral("SBGLBridge"));
        connect(bridgeServer_, &QLocalServer::newConnection, this, [this] {
            if (bridgeSocket_) { bridgeSocket_->disconnect(this); bridgeSocket_->deleteLater(); }
            bridgeSocket_ = bridgeServer_->nextPendingConnection();
            bridgeConnected_ = bridgeSocket_ != nullptr;
            if (bridgeSocket_) {
                connect(bridgeSocket_, &QLocalSocket::readyRead, this, [this] { readBridgeMessages(); });
                connect(bridgeSocket_, &QLocalSocket::disconnected, this, [this] {
                    bridgeConnected_ = false; bridgeTelemetry_ = {}; bridgePositionInitialized_ = false;
                    if (bridgeSocket_) bridgeSocket_->deleteLater();
                    bridgeSocket_ = nullptr; syncOverlay();
                });
            }
        });
        overlayTimer_ = new QTimer(this);
        connect(overlayTimer_, &QTimer::timeout, this, [this] { syncOverlay(); });
        overlayTimer_->start(16);
    }

public:
    ~MainWindow() override {
        if (overlay_) {
            overlay_->close();
            delete overlay_;
            overlay_ = nullptr;
        }
    }

private:
    void createSettingsMenu() {
        settingsMenu_ = menuBar()->addMenu(bspvision::uiText("Settings", "Настройки"));
        languageMenu_ = settingsMenu_->addMenu(bspvision::uiText("Language", "Язык"));
        auto *languageGroup = new QActionGroup(this);
        languageGroup->setExclusive(true);

        englishAction_ = languageMenu_->addAction(QStringLiteral("English"));
        englishAction_->setCheckable(true);
        languageGroup->addAction(englishAction_);
        russianAction_ = languageMenu_->addAction(QString::fromUtf8("Русский"));
        russianAction_->setCheckable(true);
        languageGroup->addAction(russianAction_);

        connect(englishAction_, &QAction::triggered, this, [this] {
            bspvision::setUiLanguage(bspvision::UiLanguage::English);
            QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
            settings.setValue(QStringLiteral("language"), QStringLiteral("en"));
            rebuildInterface();
        });
        connect(russianAction_, &QAction::triggered, this, [this] {
            bspvision::setUiLanguage(bspvision::UiLanguage::Russian);
            QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
            settings.setValue(QStringLiteral("language"), QStringLiteral("ru"));
            rebuildInterface();
        });
    }

    void updateSettingsMenuText() {
        settingsMenu_->setTitle(bspvision::uiText("Settings", "Настройки"));
        languageMenu_->setTitle(bspvision::uiText("Language", "Язык"));
        englishAction_->setText(QStringLiteral("English"));
        russianAction_->setText(QString::fromUtf8("Русский"));
        englishAction_->setChecked(bspvision::uiLanguage() == bspvision::UiLanguage::English);
        russianAction_->setChecked(bspvision::uiLanguage() == bspvision::UiLanguage::Russian);
    }

    void rebuildInterface() {
        if (screenCaptureTimer_) screenCaptureTimer_->stop();
        screenReader_.stop();

        const auto value = [](QDoubleSpinBox *box, double fallback) {
            return box ? box->value() : fallback;
        };
        const double ox = value(originX_, 0.0), oy = value(originY_, 0.0), oz = value(originZ_, 32.0);
        const double tx = value(targetX_, 512.0), ty = value(targetY_, 0.0), tz = value(targetZ_, 32.0);
        const double speed = value(speed_, 1000.0), gravity = value(gravity_, 800.0);
        const double yaw = value(yaw_, 0.0), pitch = value(pitch_, 25.0), flight = value(flightTime_, 2.0);
        const double initialYaw = value(currentYawInit_, 0.0), initialPitch = value(currentPitchInit_, 0.0);
        const double overlayFov = value(overlayFov_, 90.0);
        const int previousTab = tabs_ ? tabs_->currentIndex() : 0;
        const bool hadTrajectory = trajectoryCalculated_;

        if (QWidget *oldCentral = takeCentralWidget()) delete oldCentral;
        tabs_ = new QTabWidget(this);
        setCentralWidget(tabs_);

        bspSummary_ = nullptr;
        coordinateLabel_ = nullptr;
        lumpTable_ = nullptr;
        mapView_ = nullptr;
        meshView_ = nullptr;
        ballisticsView_ = nullptr;
        ballisticsSummary_ = nullptr;
        liveStatusLabel_ = nullptr;
        liveTelemetryLabel_ = nullptr;
        screenReaderStatusLabel_ = nullptr;
        screenReaderEnabledCheck_ = nullptr;
        screenCaptureX_ = screenCaptureY_ = screenCaptureWidth_ = screenCaptureHeight_ = nullptr;
        useScreenPositionButton_ = nullptr;
        showSkyboxCheck_ = nullptr;
        currentYawInit_ = currentPitchInit_ = overlayFov_ = nullptr;
        originX_ = originY_ = originZ_ = nullptr;
        targetX_ = targetY_ = targetZ_ = nullptr;
        speed_ = gravity_ = yaw_ = pitch_ = flightTime_ = nullptr;

        buildBspTab(tabs_);
        buildBallisticsTab(tabs_);
        originX_->setValue(ox); originY_->setValue(oy); originZ_->setValue(oz);
        targetX_->setValue(tx); targetY_->setValue(ty); targetZ_->setValue(tz);
        speed_->setValue(speed); gravity_->setValue(gravity);
        yaw_->setValue(yaw); pitch_->setValue(pitch); flightTime_->setValue(flight);
        currentYawInit_->setValue(initialYaw); currentPitchInit_->setValue(initialPitch);
        overlayFov_->setValue(overlayFov);

        updateSettingsMenuText();
        setWindowTitle(bspvision::uiText("Source BSP Geometry Lab", "Source BSP: Геометрия и баллистика"));
        if (bspFile_.isLoaded()) refreshMapViews();
        tabs_->setCurrentIndex(std::clamp(previousTab, 0, tabs_->count() - 1));
        if (hadTrajectory) calculateArc(false);
        syncOverlay();
        statusBar()->showMessage(bspvision::uiText(
            "Ready. Load a Source BSP v20 map to inspect geometry and plan trajectories.",
            "Готово. Загрузите карту Source BSP v20 для просмотра геометрии и расчёта траекторий."));
    }

    void refreshMapViews() {
        if (!bspFile_.isLoaded() || !mapView_ || !meshView_ || !ballisticsView_) return;
        const QVector<bspvision::BspPolygon> polygons = bspFile_.facePolygons(showSkybox_);
        mapView_->setMap(bspFile_.vertices(), polygons);
        meshView_->setMap(bspFile_.vertices(), polygons);
        ballisticsView_->setMap(bspFile_.vertices(), polygons);
        bspSummary_->setText(bspFile_.summary());
        populateLumpTable();
        if (showSkyboxCheck_) showSkyboxCheck_->setEnabled(true);
        coordinateLabel_->setText(bspvision::uiText(
            "Click the map preview to inspect XY coordinates. Z is ambiguous because maps can contain multiple floors. Sky-textured faces are hidden by default.",
            "Нажмите на карту, чтобы выбрать X/Y. Координата Z неоднозначна из-за нескольких этажей. Поверхности с sky-материалами скрыты по умолчанию."));
    }

    QString localizeLumpName(const QString &name) const {
        if (bspvision::uiLanguage() != bspvision::UiLanguage::Russian) return name;
        static const QHash<QString, QString> names = {
            {QStringLiteral("Entities"), QString::fromUtf8("Сущности")},
            {QStringLiteral("Planes"), QString::fromUtf8("Плоскости")},
            {QStringLiteral("Texture data"), QString::fromUtf8("Данные текстур")},
            {QStringLiteral("Vertices"), QString::fromUtf8("Вершины")},
            {QStringLiteral("Visibility"), QString::fromUtf8("Видимость")},
            {QStringLiteral("Nodes"), QString::fromUtf8("Узлы")},
            {QStringLiteral("Texture info"), QString::fromUtf8("Параметры текстур")},
            {QStringLiteral("Faces"), QString::fromUtf8("Грани")},
            {QStringLiteral("Lighting"), QString::fromUtf8("Освещение")},
            {QStringLiteral("Occlusion"), QString::fromUtf8("Окклюзия")},
            {QStringLiteral("Leaves"), QString::fromUtf8("Листья BSP")},
            {QStringLiteral("Face IDs"), QStringLiteral("Face ID")},
            {QStringLiteral("Edges"), QString::fromUtf8("Рёбра")},
            {QStringLiteral("Surf edges"), QString::fromUtf8("Направленные рёбра")},
            {QStringLiteral("Models"), QString::fromUtf8("Модели")},
            {QStringLiteral("World lights"), QString::fromUtf8("Источники света мира")},
            {QStringLiteral("Leaf faces"), QString::fromUtf8("Грани листьев")},
            {QStringLiteral("Leaf brushes"), QString::fromUtf8("Объёмы листьев")},
            {QStringLiteral("Brush sides"), QString::fromUtf8("Стороны брашей")},
            {QStringLiteral("Areas"), QString::fromUtf8("Области")},
            {QStringLiteral("Area portals"), QString::fromUtf8("Порталы областей")},
            {QStringLiteral("Portals"), QString::fromUtf8("Порталы")},
            {QStringLiteral("Clusters"), QString::fromUtf8("Кластеры")},
            {QStringLiteral("Portal vertices"), QString::fromUtf8("Вершины порталов")},
            {QStringLiteral("Cluster portals"), QString::fromUtf8("Порталы кластеров")},
            {QStringLiteral("Displacement info"), QString::fromUtf8("Данные смещений")},
            {QStringLiteral("Original faces"), QString::fromUtf8("Исходные грани")},
            {QStringLiteral("Physics displacement"), QString::fromUtf8("Физические смещения")},
            {QStringLiteral("Physics collision"), QString::fromUtf8("Физические коллизии")},
            {QStringLiteral("Vertex normals"), QString::fromUtf8("Нормали вершин")},
            {QStringLiteral("Vertex normal indices"), QString::fromUtf8("Индексы нормалей")},
            {QStringLiteral("Displacement vertices"), QString::fromUtf8("Вершины смещений")},
            {QStringLiteral("Packed files"), QString::fromUtf8("Упакованные файлы")},
            {QStringLiteral("Cubemaps"), QStringLiteral("Cubemaps")},
            {QStringLiteral("Texture string data"), QString::fromUtf8("Строки текстур")},
            {QStringLiteral("Texture string table"), QString::fromUtf8("Таблица строк текстур")},
            {QStringLiteral("Overlays"), QString::fromUtf8("Накладки")},
            {QStringLiteral("Displacement triangles"), QString::fromUtf8("Треугольники смещений")},
            {QStringLiteral("Map flags"), QString::fromUtf8("Флаги карты")},
            {QStringLiteral("Brushes"), QString::fromUtf8("Браши")},
            {QStringLiteral("Displacement lightmap alphas"), QString::fromUtf8("Альфа lightmap смещений")},
            {QStringLiteral("Displacement lightmap samples"), QString::fromUtf8("Семплы lightmap смещений")},
            {QStringLiteral("Game lump"), QString::fromUtf8("Игровой lump")},
            {QStringLiteral("Leaf water data"), QString::fromUtf8("Данные воды листьев")},
            {QStringLiteral("Primitives"), QString::fromUtf8("Примитивы")},
            {QStringLiteral("Primitive vertices"), QString::fromUtf8("Вершины примитивов")},
            {QStringLiteral("Primitive indices"), QString::fromUtf8("Индексы примитивов")},
            {QStringLiteral("Clip portal vertices"), QString::fromUtf8("Вершины clip-порталов")},
            {QStringLiteral("Leaf min distance to water"), QString::fromUtf8("Минимальная дистанция листа до воды")},
            {QStringLiteral("Face macro texture info"), QString::fromUtf8("Макротекстуры граней")},
            {QStringLiteral("Physics collision surface"), QString::fromUtf8("Поверхности физ. коллизий")},
            {QStringLiteral("Water overlays"), QString::fromUtf8("Водные накладки")},
            {QStringLiteral("Leaf ambient index HDR"), QString::fromUtf8("Индексы ambient HDR листьев")},
            {QStringLiteral("Leaf ambient index"), QString::fromUtf8("Индексы ambient листьев")},
            {QStringLiteral("Lighting HDR"), QString::fromUtf8("Освещение HDR")},
            {QStringLiteral("World lights HDR"), QString::fromUtf8("Источники света HDR")},
            {QStringLiteral("Leaf ambient lighting HDR"), QString::fromUtf8("Ambient-освещение HDR листьев")},
            {QStringLiteral("Leaf ambient lighting"), QString::fromUtf8("Ambient-освещение листьев")},
            {QStringLiteral("XZIP packed file"), QString::fromUtf8("Упакованный XZIP-файл")},
            {QStringLiteral("Faces HDR"), QString::fromUtf8("Грани HDR")},
            {QStringLiteral("Overlay fades"), QString::fromUtf8("Затухание накладок")},
            {QStringLiteral("Overlay system levels"), QString::fromUtf8("Уровни системы накладок")},
            {QStringLiteral("Physics levels"), QString::fromUtf8("Уровни физики")},
            {QStringLiteral("Displacement multiblend"), QString::fromUtf8("Многослойные смешения смещений")}
        };
        return names.value(name, name);
    }

    void buildBspTab(QTabWidget *tabs) {
        auto *tab = new QWidget(tabs);
        auto *layout = new QVBoxLayout(tab);
        auto *controls = new QHBoxLayout();
        auto *loadBsp = new QPushButton(bspvision::uiText("Load Source BSP v20…", "Загрузить Source BSP v20…"), tab);
        controls->addWidget(loadBsp);
        showSkyboxCheck_ = new QCheckBox(bspvision::uiText("Show skybox surfaces", "Показывать поверхности скайбокса"), tab);
        showSkyboxCheck_->setChecked(showSkybox_);
        showSkyboxCheck_->setEnabled(bspFile_.isLoaded());
        controls->addWidget(showSkyboxCheck_);
        controls->addStretch();
        layout->addLayout(controls);
        connect(showSkyboxCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            showSkybox_ = checked;
            if (bspFile_.isLoaded()) refreshMapViews();
        });

        bspSummary_ = new QLabel(bspvision::uiText(
            "No map loaded. This parser currently accepts Source BSP version 20.",
            "Карта не загружена. Поддерживается Source BSP версии 20."), tab);
        bspSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bspSummary_->setWordWrap(true);
        layout->addWidget(bspSummary_);

        auto *content = new QHBoxLayout();
        auto *previewTabs = new QTabWidget(tab);
        mapView_ = new bspvision::BspMapView(previewTabs);
        meshView_ = new bspvision::Bsp3DView(previewTabs);
        previewTabs->addTab(mapView_, bspvision::uiText("Top-down XY", "Сверху XY"));
        previewTabs->addTab(meshView_, bspvision::uiText("Orbit 3D", "3D-орбита"));
        content->addWidget(previewTabs, 3);

        lumpTable_ = new QTableWidget(tab);
        lumpTable_->setColumnCount(4);
        lumpTable_->setHorizontalHeaderLabels({
            bspvision::uiText("Lump", "Lump"), bspvision::uiText("Bytes", "Байты"),
            bspvision::uiText("Records", "Записи"), bspvision::uiText("Ver.", "Вер.")});
        lumpTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        lumpTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        lumpTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        lumpTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        lumpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        lumpTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        lumpTable_->setAlternatingRowColors(true);
        content->addWidget(lumpTable_, 2);
        layout->addLayout(content, 1);

        coordinateLabel_ = new QLabel(bspvision::uiText(
            "Click the map preview to inspect an XY coordinate. Z is not unique in a top-down view.",
            "Нажмите на карту, чтобы посмотреть координаты X/Y. В виде сверху невозможно однозначно определить Z."),
            tab);
        coordinateLabel_->setWordWrap(true);
        layout->addWidget(coordinateLabel_);
        tabs->addTab(tab, bspvision::uiText("BSP geometry", "Геометрия BSP"));

        connect(loadBsp, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this,
                bspvision::uiText("Open Source BSP v20 map", "Открыть карту Source BSP v20"), {},
                bspvision::uiText("BSP maps (*.bsp);;All files (*.*)", "Карты BSP (*.bsp);;Все файлы (*.*)"));
            if (path.isEmpty()) return;

            QString error;
            if (!bspFile_.load(path, &error)) {
                QMessageBox::warning(this, bspvision::uiText("BSP load failed", "Ошибка загрузки BSP"), error);
                return;
            }
            loadedMapName_ = QFileInfo(path).completeBaseName();
            if (showSkyboxCheck_) showSkyboxCheck_->setEnabled(true);
            refreshMapViews();
            statusBar()->showMessage(bspvision::uiText("Loaded BSP: %1", "Загружена карта BSP: %1")
                .arg(QFileInfo(path).fileName()));
        });

        mapView_->pointSelected = [this](double x, double y) {
            coordinateLabel_->setText(bspvision::uiText(
                "Selected top-down coordinate: X = %1, Y = %2. Set Z manually for the correct floor.",
                "Выбраны координаты сверху: X = %1, Y = %2. Укажите Z вручную для нужного этажа.")
                .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
            if (pickMode_ == 1) {
                originX_->setValue(x); originY_->setValue(y);
                trajectoryCalculated_ = false;
                pickMode_ = 0;
                statusBar()->showMessage(bspvision::uiText("Launch point set from map. Set launch Z manually.", "Точка запуска выбрана на карте. Укажите Z вручную."));
            } else if (pickMode_ == 2) {
                targetX_->setValue(x); targetY_->setValue(y);
                targetDefined_ = true;
                trajectoryCalculated_ = false;
                pickMode_ = 0;
                syncOverlay();
                statusBar()->showMessage(bspvision::uiText("Target point set from map. Set target Z manually if needed.", "Цель выбрана на карте. При необходимости укажите Z вручную."));
            }
        };
    }

    QDoubleSpinBox *makeCoordinate(double value, QWidget *parent) {
        auto *spin = new QDoubleSpinBox(parent);
        spin->setRange(-1000000.0, 1000000.0);
        spin->setDecimals(2);
        spin->setSingleStep(16.0);
        spin->setValue(value);
        spin->setMinimumWidth(100);
        return spin;
    }

    void buildBallisticsTab(QTabWidget *tabs) {
        auto *tab = new QWidget(tabs);
        auto *layout = new QVBoxLayout(tab);
        auto *controls = new QHBoxLayout();

        auto *liveGroup = new QGroupBox(bspvision::uiText(
            "Native SBGL Bridge · in-game HUD", "Нативный SBGL Bridge · игровой HUD"), tab);
        auto *liveLayout = new QVBoxLayout(liveGroup);
        currentYawInit_ = makeCoordinate(0.0, liveGroup);
        currentPitchInit_ = makeCoordinate(0.0, liveGroup);
        currentYawInit_->hide();
        currentPitchInit_->hide();
        overlayFov_ = makeCoordinate(90.0, liveGroup);
        overlayFov_->setRange(30.0, 140.0);
        overlayFov_->setSingleStep(5.0);
        auto *fovRow = new QHBoxLayout();
        fovRow->addWidget(new QLabel(bspvision::uiText("Horizontal FOV °", "Горизонтальный FOV °"), liveGroup));
        fovRow->addWidget(overlayFov_);
        auto *startOverlay = new QPushButton(bspvision::uiText("Connect / show overlay", "Подключить / показать оверлей"), liveGroup);
        auto *stopOverlay = new QPushButton(bspvision::uiText("Hide overlay", "Скрыть оверлей"), liveGroup);
        fovRow->addWidget(startOverlay);
        fovRow->addWidget(stopOverlay);
        fovRow->addStretch();
        liveLayout->addLayout(fovRow);
        liveStatusLabel_ = new QLabel(bspvision::uiText(
            "Waiting for the native bridge. Install the binary module and Lua adapter, then restart GMod.",
            "Ожидаем нативный мост. Установите бинарный модуль и Lua-адаптер, затем перезапустите GMod."), liveGroup);
        liveStatusLabel_->setWordWrap(true);
        liveLayout->addWidget(liveStatusLabel_);
        liveTelemetryLabel_ = new QLabel(liveGroup);
        liveTelemetryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        liveTelemetryLabel_->setWordWrap(true);
        liveLayout->addWidget(liveTelemetryLabel_);
        layout->addWidget(liveGroup);
        connect(startOverlay, &QPushButton::clicked, this, [this] { startExternalOverlay(); });
        connect(stopOverlay, &QPushButton::clicked, this, [this] { stopExternalOverlay(); });
        connect(overlayFov_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { syncOverlay(); });

        auto *screenGroup = new QGroupBox(
            bspvision::uiText("Screen fallback · cl_showpos (no Lua)", "Резервный экранный канал · cl_showpos (без Lua)"), tab);
        auto *screenLayout = new QVBoxLayout(screenGroup);
        QSettings screenSettings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
        QScreen *primary = QGuiApplication::primaryScreen();
        const QSize primarySize = primary ? primary->size() : QSize(1920, 1080);

        screenReaderEnabledCheck_ = new QCheckBox(
            bspvision::uiText("Enable DXGI template reader", "Включить шаблонное распознавание через DXGI"), screenGroup);
        screenReaderEnabledCheck_->setChecked(screenSettings.value(QStringLiteral("clShowPos/enabled"), false).toBool());
        screenLayout->addWidget(screenReaderEnabledCheck_);

        auto *cropRow = new QHBoxLayout();
        const auto makeScreenField = [screenGroup](int minimum, int maximum, int value) {
            auto *field = new QSpinBox(screenGroup);
            field->setRange(minimum, maximum);
            field->setValue(value);
            field->setMaximumWidth(100);
            return field;
        };
        screenCaptureX_ = makeScreenField(0, 32768, screenSettings.value(QStringLiteral("clShowPos/x"), 0).toInt());
        screenCaptureY_ = makeScreenField(0, 16384, screenSettings.value(QStringLiteral("clShowPos/y"), 0).toInt());
        screenCaptureWidth_ = makeScreenField(200, 4096,
            screenSettings.value(QStringLiteral("clShowPos/width"), std::min(640, primarySize.width())).toInt());
        screenCaptureHeight_ = makeScreenField(45, 512,
            screenSettings.value(QStringLiteral("clShowPos/height"), std::min(100, primarySize.height())).toInt());

        cropRow->addWidget(new QLabel(QStringLiteral("X"), screenGroup));
        cropRow->addWidget(screenCaptureX_);
        cropRow->addWidget(new QLabel(QStringLiteral("Y"), screenGroup));
        cropRow->addWidget(screenCaptureY_);
        cropRow->addWidget(new QLabel(bspvision::uiText("Width", "Ширина"), screenGroup));
        cropRow->addWidget(screenCaptureWidth_);
        cropRow->addWidget(new QLabel(bspvision::uiText("Height", "Высота"), screenGroup));
        cropRow->addWidget(screenCaptureHeight_);
        auto *startScreenReader = new QPushButton(
            bspvision::uiText("Apply crop / start", "Применить область / запуск"), screenGroup);
        auto *stopScreenReader = new QPushButton(
            bspvision::uiText("Stop", "Остановить"), screenGroup);
        cropRow->addWidget(startScreenReader);
        cropRow->addWidget(stopScreenReader);
        cropRow->addStretch();
        screenLayout->addLayout(cropRow);

        screenReaderStatusLabel_ = new QLabel(
            bspvision::uiText(
                "Uses the primary monitor. Enable cl_showpos 1 in the game; the crop must include pos:, ang: and vel:.",
                "Используется основной монитор. Включите cl_showpos 1; область должна включать строки pos:, ang: и vel:."), screenGroup);
        screenReaderStatusLabel_->setWordWrap(true);
        screenLayout->addWidget(screenReaderStatusLabel_);

        useScreenPositionButton_ = new QPushButton(
            bspvision::uiText("Use last pos as launch point", "Использовать pos как точку запуска"), screenGroup);
        useScreenPositionButton_->setEnabled(false);
        screenLayout->addWidget(useScreenPositionButton_);
        auto *screenNote = new QLabel(
            bspvision::uiText(
                "The reader uses classic pixel-template matching, not OCR/NN. pos is Source MainViewOrigin and may differ from the weapon muzzle; verify it before using it as the launch point.",
                "Чтение выполняется по пиксельным шаблонам, без OCR/нейросети. pos — это MainViewOrigin Source, он может отличаться от дульного среза; проверьте координаты перед использованием."), screenGroup);
        screenNote->setWordWrap(true);
        screenLayout->addWidget(screenNote);
        layout->addWidget(screenGroup);

        const auto persistScreenCrop = [this] {
            QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
            if (screenCaptureX_) settings.setValue(QStringLiteral("clShowPos/x"), screenCaptureX_->value());
            if (screenCaptureY_) settings.setValue(QStringLiteral("clShowPos/y"), screenCaptureY_->value());
            if (screenCaptureWidth_) settings.setValue(QStringLiteral("clShowPos/width"), screenCaptureWidth_->value());
            if (screenCaptureHeight_) settings.setValue(QStringLiteral("clShowPos/height"), screenCaptureHeight_->value());
        };
        for (QSpinBox *field : {screenCaptureX_, screenCaptureY_, screenCaptureWidth_, screenCaptureHeight_}) {
            connect(field, qOverload<int>(&QSpinBox::valueChanged), this, [persistScreenCrop](int) { persistScreenCrop(); });
        }
        connect(screenReaderEnabledCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
            QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
            settings.setValue(QStringLiteral("clShowPos/enabled"), enabled);
            if (enabled) startClShowPosReader();
            else stopClShowPosReader();
        });
        connect(startScreenReader, &QPushButton::clicked, this, [this] { startClShowPosReader(); });
        connect(stopScreenReader, &QPushButton::clicked, this, [this] { stopClShowPosReader(); });
        connect(useScreenPositionButton_, &QPushButton::clicked, this, [this] {
            ClShowPosSample sample;
            if (!screenReader_.latestSample(&sample) || !originX_ || !originY_ || !originZ_) return;
            originX_->setValue(sample.position.x());
            originY_->setValue(sample.position.y());
            originZ_->setValue(sample.position.z());
            statusBar()->showMessage(bspvision::uiText(
                "Copied cl_showpos position. Verify camera-origin vs muzzle offset for this vehicle.",
                "Координаты pos скопированы. Проверьте смещение камеры относительно орудия этой техники."));
        });
        if (screenReaderEnabledCheck_->isChecked()) {
            QTimer::singleShot(0, this, [this] { startClShowPosReader(); });
        }

        auto *launchGroup = new QGroupBox(bspvision::uiText("Launch point · Source world units", "Точка запуска · единицы Source"), tab);
        auto *launchForm = new QFormLayout(launchGroup);
        originX_ = makeCoordinate(0.0, launchGroup);
        originY_ = makeCoordinate(0.0, launchGroup);
        originZ_ = makeCoordinate(32.0, launchGroup);
        launchForm->addRow(QStringLiteral("X"), originX_);
        launchForm->addRow(QStringLiteral("Y"), originY_);
        launchForm->addRow(bspvision::uiText("Z / height", "Z / высота"), originZ_);
        auto *pickOrigin = new QPushButton(bspvision::uiText("Pick XY on map…", "Выбрать X/Y на карте…"), launchGroup);
        launchForm->addRow(pickOrigin);

        auto *targetGroup = new QGroupBox(bspvision::uiText("Target point", "Целевая точка"), tab);
        auto *targetForm = new QFormLayout(targetGroup);
        targetX_ = makeCoordinate(512.0, targetGroup);
        targetY_ = makeCoordinate(0.0, targetGroup);
        targetZ_ = makeCoordinate(32.0, targetGroup);
        targetForm->addRow(QStringLiteral("X"), targetX_);
        targetForm->addRow(QStringLiteral("Y"), targetY_);
        targetForm->addRow(bspvision::uiText("Z / height", "Z / высота"), targetZ_);
        auto *pickTarget = new QPushButton(bspvision::uiText("Pick XY on map…", "Выбрать X/Y на карте…"), targetGroup);
        targetForm->addRow(pickTarget);

        auto *physicsGroup = new QGroupBox(bspvision::uiText("Projectile model", "Параметры снаряда"), tab);
        auto *physicsForm = new QFormLayout(physicsGroup);
        speed_ = makeCoordinate(1000.0, physicsGroup);
        speed_->setRange(1.0, 100000.0);
        speed_->setSingleStep(50.0);
        gravity_ = makeCoordinate(800.0, physicsGroup);
        gravity_->setRange(0.0, 100000.0);
        gravity_->setSingleStep(50.0);
        yaw_ = makeCoordinate(0.0, physicsGroup);
        yaw_->setRange(-360.0, 360.0);
        yaw_->setSingleStep(5.0);
        pitch_ = makeCoordinate(25.0, physicsGroup);
        pitch_->setRange(-89.0, 89.0);
        pitch_->setSingleStep(1.0);
        flightTime_ = makeCoordinate(2.0, physicsGroup);
        flightTime_->setRange(0.05, 30.0);
        flightTime_->setSingleStep(0.25);
        physicsForm->addRow(bspvision::uiText("Speed (units/s)", "Скорость (ед./с)"), speed_);
        physicsForm->addRow(bspvision::uiText("Gravity (units/s²)", "Гравитация (ед./с²)"), gravity_);
        physicsForm->addRow(bspvision::uiText("Yaw / azimuth (°)", "Азимут (°)"), yaw_);
        physicsForm->addRow(bspvision::uiText("Pitch / elevation (°)", "Угол возвышения (°)"), pitch_);
        physicsForm->addRow(bspvision::uiText("Flight time (s)", "Время полёта (с)"), flightTime_);

        controls->addWidget(launchGroup, 1);
        controls->addWidget(targetGroup, 1);
        controls->addWidget(physicsGroup, 1);
        layout->addLayout(controls);

        auto *actions = new QHBoxLayout();
        auto *calculate = new QPushButton(bspvision::uiText("Calculate current arc", "Рассчитать текущую дугу"), tab);
        auto *solveLow = new QPushButton(bspvision::uiText("Solve target · low arc", "Решить для цели · низкая дуга"), tab);
        auto *solveHigh = new QPushButton(bspvision::uiText("Solve target · high arc", "Решить для цели · высокая дуга"), tab);
        auto *clear = new QPushButton(bspvision::uiText("Clear path / aim target", "Очистить траекторию / цель"), tab);
        actions->addWidget(calculate);
        actions->addWidget(solveLow);
        actions->addWidget(solveHigh);
        actions->addWidget(clear);
        actions->addStretch();
        layout->addLayout(actions);

        ballisticsView_ = new bspvision::BallisticsView(tab);
        layout->addWidget(ballisticsView_, 1);
        ballisticsSummary_ = new QLabel(bspvision::uiText(
            "Ideal projectile model only: tune speed and gravity to the game. Obstacles, drag, splash radius and engine-specific projectile behavior are not simulated.",
            "Используется идеальная модель снаряда. Подберите скорость и гравитацию под игру. Столкновения, препятствия, сопротивление воздуха и особенности конкретного оружия не моделируются."), tab);
        ballisticsSummary_->setWordWrap(true);
        layout->addWidget(ballisticsSummary_);
        tabs->addTab(tab, bspvision::uiText("Ballistic lab", "Баллистика"));

        connect(pickOrigin, &QPushButton::clicked, this, [this, tabs] {
            pickMode_ = 1;
            tabs->setCurrentIndex(0);
            statusBar()->showMessage(bspvision::uiText("Click the top-down XY map to set launch X/Y; set Z in Ballistic lab.", "Нажмите на карту сверху для выбора X/Y запуска; координату Z задайте во вкладке баллистики."));
        });
        connect(pickTarget, &QPushButton::clicked, this, [this, tabs] {
            pickMode_ = 2;
            tabs->setCurrentIndex(0);
            statusBar()->showMessage(bspvision::uiText("Click the top-down XY map to set target X/Y; set Z in Ballistic lab.", "Нажмите на карту сверху для выбора X/Y цели; координату Z задайте во вкладке баллистики."));
        });
        connect(calculate, &QPushButton::clicked, this, [this] { calculateArc(false); });
        connect(solveLow, &QPushButton::clicked, this, [this] { calculateArc(true, false); });
        connect(solveHigh, &QPushButton::clicked, this, [this] { calculateArc(true, true); });
        connect(clear, &QPushButton::clicked, this, [this] {
            ballisticsView_->clearTrajectory();
            trajectoryCalculated_ = false;
            targetDefined_ = false;
            ballisticsSummary_->setText(bspvision::uiText("Trajectory and overlay target cleared.", "Траектория и цель оверлея очищены."));
            syncOverlay();
        });

        auto invalidateSolution = [this] {
            trajectoryCalculated_ = false;
            if (ballisticsView_) ballisticsView_->clearTrajectory();
            if (ballisticsSummary_) {
                ballisticsSummary_->setText(bspvision::uiText(
                    "Parameters changed. Recalculate the trajectory to refresh the overlay.",
                    "Параметры изменены. Пересчитайте траекторию, чтобы обновить оверлей."));
            }
            syncOverlay();
        };
        const QList<QDoubleSpinBox *> solutionInputs = {
            originX_, originY_, originZ_, targetX_, targetY_, targetZ_,
            speed_, gravity_, yaw_, pitch_, flightTime_
        };
        for (QDoubleSpinBox *field : solutionInputs) {
            connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [invalidateSolution](double) { invalidateSolution(); });
        }
    }

    void startClShowPosReader() {
        if (screenReaderEnabledCheck_ && !screenReaderEnabledCheck_->isChecked()) {
            screenReaderEnabledCheck_->setChecked(true);
            return; // The toggled handler performs the initial start.
        }
        if (!screenCaptureX_ || !screenCaptureY_ || !screenCaptureWidth_ || !screenCaptureHeight_) return;

        const QRect region(screenCaptureX_->value(), screenCaptureY_->value(),
                           screenCaptureWidth_->value(), screenCaptureHeight_->value());
        QString error;
        if (!screenReader_.start(region, &error)) {
            if (screenReaderStatusLabel_) screenReaderStatusLabel_->setText(error);
            if (screenCaptureTimer_) screenCaptureTimer_->stop();
            syncOverlay();
            return;
        }
        if (screenCaptureTimer_) screenCaptureTimer_->start();
        if (screenReaderStatusLabel_) screenReaderStatusLabel_->setText(screenReader_.lastStatus());
        if (useScreenPositionButton_) useScreenPositionButton_->setEnabled(false);
        syncOverlay();
    }

    void stopClShowPosReader() {
        if (screenReaderEnabledCheck_ && screenReaderEnabledCheck_->isChecked()) {
            screenReaderEnabledCheck_->setChecked(false);
            return; // The toggled handler performs the stop.
        }
        if (screenCaptureTimer_) screenCaptureTimer_->stop();
        screenReader_.stop();
        if (screenReaderStatusLabel_) screenReaderStatusLabel_->setText(screenReader_.lastStatus());
        if (useScreenPositionButton_) useScreenPositionButton_->setEnabled(false);
        syncOverlay();
    }

    void pollClShowPosReader() {
        if (!screenReader_.isRunning()) return;
        ClShowPosSample sample;
        QString status;
        if (screenReader_.poll(&sample, &status)) {
            if (!bridgeConnected_) {
                if (currentYawInit_) currentYawInit_->setValue(sample.angles.y());
                if (currentPitchInit_) currentPitchInit_->setValue(sample.angles.x());
            }
            if (useScreenPositionButton_) useScreenPositionButton_->setEnabled(true);
            if (liveTelemetryLabel_ && !bridgeConnected_) {
                liveTelemetryLabel_->setText(
                    QStringLiteral("cl_showpos · pos (%1, %2, %3) · ang p/y/r (%4, %5, %6) · confidence %7%")
                    .arg(sample.position.x(), 0, 'f', 2)
                    .arg(sample.position.y(), 0, 'f', 2)
                    .arg(sample.position.z(), 0, 'f', 2)
                    .arg(sample.angles.x(), 0, 'f', 2)
                    .arg(sample.angles.y(), 0, 'f', 2)
                    .arg(sample.angles.z(), 0, 'f', 2)
                    .arg(qRound(sample.confidence * 100.0)));
            }
        }
        if (screenReaderStatusLabel_) {
            const QString nextStatus = status.isEmpty() ? screenReader_.lastStatus() : status;
            if (screenReaderStatusLabel_->text() != nextStatus)
                screenReaderStatusLabel_->setText(nextStatus);
        }
        syncOverlay();
    }

    void calculateArc(bool solveTarget, bool highArc = false) {
        const double ox = originX_->value(), oy = originY_->value(), oz = originZ_->value();
        const double tx = targetX_->value(), ty = targetY_->value(), tz = targetZ_->value();
        const double speed = speed_->value(), g = gravity_->value();
        double yaw = qDegreesToRadians(yaw_->value());
        double pitch = qDegreesToRadians(pitch_->value());
        double duration = flightTime_->value();
        trajectoryCalculated_ = false;
        if (ballisticsView_) ballisticsView_->clearTrajectory();
        syncOverlay();
        targetDefined_ = targetDefined_ || solveTarget;

        if (solveTarget) {
            const double dx = tx - ox, dy = ty - oy, dz = tz - oz;
            const double range = std::hypot(dx, dy);
            if (range < 0.01) {
                QMessageBox::warning(this, bspvision::uiText("Ballistic solver", "Расчёт баллистики"),
                    bspvision::uiText("Launch point and target must differ in XY.", "Точка запуска и цель должны различаться по X/Y."));
                return;
            }
            yaw = std::atan2(dy, dx);
            if (g <= 0.0) {
                pitch = std::atan2(dz, range);
            } else {
                const double v2 = speed * speed;
                const double discriminant = v2 * v2 - g * (g * range * range + 2.0 * dz * v2);
                if (discriminant < 0.0) {
                    QMessageBox::information(this, bspvision::uiText("No ballistic solution", "Нет баллистического решения"),
                        bspvision::uiText("The target is outside the ideal projectile envelope for this speed and gravity. Increase speed, reduce gravity, or choose a nearer target.", "Цель недостижима при заданной скорости и гравитации в идеальной модели. Увеличьте скорость, уменьшите гравитацию или выберите цель ближе."));
                    return;
                }
                const double root = std::sqrt(discriminant);
                const double tangent = (v2 + (highArc ? root : -root)) / (g * range);
                pitch = std::atan(tangent);
            }
            const double horizontalSpeed = speed * std::cos(pitch);
            if (horizontalSpeed <= 0.01) {
                QMessageBox::warning(this, bspvision::uiText("Ballistic solver", "Расчёт баллистики"),
                    bspvision::uiText("The computed elevation is too steep for a stable horizontal range.", "Полученный угол слишком велик для устойчивого расчёта дальности."));
                return;
            }
            duration = range / horizontalSpeed;
            yaw_->setValue(qRadiansToDegrees(yaw));
            pitch_->setValue(qRadiansToDegrees(pitch));
            flightTime_->setValue(std::clamp(duration, 0.05, 30.0));
            duration = flightTime_->value();
        }

        const double vx = speed * std::cos(pitch) * std::cos(yaw);
        const double vy = speed * std::cos(pitch) * std::sin(yaw);
        const double vz = speed * std::sin(pitch);
        QVector<QVector3D> points;
        const int steps = 100;
        points.reserve(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            const double t = duration * static_cast<double>(i) / steps;
            points.push_back(QVector3D(
                static_cast<float>(ox + vx * t),
                static_cast<float>(oy + vy * t),
                static_cast<float>(oz + vz * t - 0.5 * g * t * t)));
        }
        const QVector3D origin(static_cast<float>(ox), static_cast<float>(oy), static_cast<float>(oz));
        const QVector3D target(static_cast<float>(tx), static_cast<float>(ty), static_cast<float>(tz));
        ballisticsView_->setTrajectory(points, origin, target, targetDefined_);
        trajectoryCalculated_ = true;
        const QVector3D end = points.last();
        const double miss = targetDefined_ ? (end - target).length() : 0.0;
        QString summary = bspvision::uiText(
            "Yaw %1° · elevation %2° · flight %3 s · endpoint X %4, Y %5, Z %6.",
            "Азимут %1° · угол возвышения %2° · время полёта %3 с · конечная точка X %4, Y %5, Z %6.")
            .arg(qRadiansToDegrees(yaw), 0, 'f', 2)
            .arg(qRadiansToDegrees(pitch), 0, 'f', 2)
            .arg(duration, 0, 'f', 2)
            .arg(end.x(), 0, 'f', 1).arg(end.y(), 0, 'f', 1).arg(end.z(), 0, 'f', 1);
        if (targetDefined_) summary += bspvision::uiText(" Ideal endpoint error: %1 units.", " Ошибка в конечной точке: %1 ед.").arg(miss, 0, 'f', 1);
        summary += bspvision::uiText(" Green = launch, red = target, amber = trajectory. Live camera data requires the native bridge.", " Зелёный = запуск, красный = цель, янтарный = траектория. Для данных камеры нужен нативный мост.");
        ballisticsSummary_->setText(summary);
        syncOverlay();
    }

    void startExternalOverlay() {
        QScreen *targetScreen = screen();
        if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
        if (targetScreen) overlay_->showOverlayOn(targetScreen->geometry());
        syncOverlay();
    }

    void stopExternalOverlay() {
        if (overlay_) overlay_->hide();
        syncOverlay();
    }

    void readBridgeMessages() {
        if (!bridgeSocket_) return;
        while (bridgeSocket_->canReadLine()) {
            const auto doc = QJsonDocument::fromJson(bridgeSocket_->readLine().trimmed());
            if (!doc.isObject()) continue;
            const auto msg = doc.object();
            if (msg.value("type").toString() != "telemetry") continue;
            bridgeTelemetry_ = msg;
            const auto player = msg.value("player").toObject();
            const auto angles = player.value("eye_angles").toObject();
            if (!angles.isEmpty()) {
                if (currentYawInit_) currentYawInit_->setValue(angles.value("yaw").toDouble());
                if (currentPitchInit_) currentPitchInit_->setValue(angles.value("pitch").toDouble());
            }
            const auto pos = player.value("position").toObject();
            if (!pos.isEmpty() && !bridgePositionInitialized_ && originX_ && originY_ && originZ_) {
                bridgePositionInitialized_ = true;
                originX_->setValue(pos.value("x").toDouble());
                originY_->setValue(pos.value("y").toDouble());
                originZ_->setValue(pos.value("z").toDouble());
            }
            const auto lvs = msg.value("lvs").toObject();
            if (liveTelemetryLabel_) liveTelemetryLabel_->setText(
                QString("LVS: %1 · handler: %2").arg(
                    lvs.value("vehicle_class").toString("not detected"),
                    lvs.value("weapon_handler_class").toString("not detected")));
        }
        syncOverlay();
    }

    void syncOverlay() {
        if (!overlay_) return;
        double cy = currentYawInit_ ? currentYawInit_->value() : 0.0;
        double cp = currentPitchInit_ ? currentPitchInit_->value() : 0.0;
        const auto player = bridgeTelemetry_.value("player").toObject();
        const auto angles = player.value("eye_angles").toObject();
        if (!angles.isEmpty()) { cy = angles.value("yaw").toDouble(cy); cp = angles.value("pitch").toDouble(cp); }
        overlay_->setCurrentAngles(cy, cp);
        overlay_->setDesiredAngles(yaw_ ? yaw_->value() : 0.0, pitch_ ? -pitch_->value() : 0.0);
        overlay_->setHorizontalFov(overlayFov_ ? overlayFov_->value() : 90.0);
        const double range = originX_ && targetX_ ? std::hypot(targetX_->value()-originX_->value(), targetY_->value()-originY_->value()) : 0.0;
        overlay_->setTargetDetails(range, flightTime_ ? flightTime_->value() : 0.0, loadedMapName_);
        const bool enabled = targetDefined_ && trajectoryCalculated_;
        overlay_->setTargetEnabled(enabled);
        QString state;
        if (bridgeConnected_) {
            state = QStringLiteral("Native bridge connected");
        } else if (screenReaderEnabledCheck_ && screenReaderEnabledCheck_->isChecked()) {
            ClShowPosSample sample;
            state = screenReader_.latestSample(&sample)
                ? QStringLiteral("Screen telemetry active (cl_showpos)")
                : screenReader_.lastStatus();
        } else {
            state = QStringLiteral("Waiting for Native Bridge or cl_showpos fallback");
        }
        if (liveStatusLabel_) liveStatusLabel_->setText(state);
        if (bridgeSocket_ && bridgeSocket_->state() == QLocalSocket::ConnectedState
            && QDateTime::currentMSecsSinceEpoch() - lastBridgeCommandMs_ >= 100) {
            lastBridgeCommandMs_ = QDateTime::currentMSecsSinceEpoch();
            QJsonObject cmd;
            cmd.insert(QStringLiteral("type"), QStringLiteral("target"));
            cmd.insert(QStringLiteral("enabled"), enabled);
            cmd.insert(QStringLiteral("yaw"), yaw_ ? yaw_->value() : 0.0);
            cmd.insert(QStringLiteral("pitch"), pitch_ ? -pitch_->value() : 0.0);
            bridgeSocket_->write(QJsonDocument(cmd).toJson(QJsonDocument::Compact) + "\n");
            bridgeSocket_->flush();
        }
        overlay_->setTrackingStatus(state);
    }

    void populateLumpTable() {
        const auto &lumps = bspFile_.lumps();
        lumpTable_->setRowCount(static_cast<int>(lumps.size()));
        for (int row = 0; row < static_cast<int>(lumps.size()); ++row) {
            const auto &lump = lumps[static_cast<size_t>(row)];
            const QString records = lump.recordCount >= 0
                ? QString::number(lump.recordCount) : QStringLiteral("—");
            const QStringList columns{
                localizeLumpName(lump.name),
                QString::number(lump.length),
                records,
                QString::number(lump.version)
            };
            for (int col = 0; col < columns.size(); ++col) {
                auto *item = new QTableWidgetItem(columns[col]);
                if (col == 0) item->setToolTip(
                    bspvision::uiText("Offset: %1 | FourCC: 0x%2", "Смещение: %1 | FourCC: 0x%2")
                        .arg(lump.offset)
                        .arg(lump.fourCC, 8, 16, QLatin1Char('0')));
                lumpTable_->setItem(row, col, item);
            }
        }
    }

    QTabWidget *tabs_ = nullptr;
    QMenu *settingsMenu_ = nullptr;
    QMenu *languageMenu_ = nullptr;
    QAction *englishAction_ = nullptr;
    QAction *russianAction_ = nullptr;
    QCheckBox *showSkyboxCheck_ = nullptr;
    bool showSkybox_ = false;
    bool trajectoryCalculated_ = false;

    QLabel *bspSummary_ = nullptr;
    QLabel *coordinateLabel_ = nullptr;
    QTableWidget *lumpTable_ = nullptr;
    bspvision::BspMapView *mapView_ = nullptr;
    bspvision::Bsp3DView *meshView_ = nullptr;
    bspvision::BallisticsView *ballisticsView_ = nullptr;
    QLabel *ballisticsSummary_ = nullptr;
    QDoubleSpinBox *originX_ = nullptr, *originY_ = nullptr, *originZ_ = nullptr;
    QDoubleSpinBox *targetX_ = nullptr, *targetY_ = nullptr, *targetZ_ = nullptr;
    QDoubleSpinBox *speed_ = nullptr, *gravity_ = nullptr, *yaw_ = nullptr, *pitch_ = nullptr, *flightTime_ = nullptr;
    QLabel *liveStatusLabel_ = nullptr, *liveTelemetryLabel_ = nullptr;
    QLabel *screenReaderStatusLabel_ = nullptr;
    QCheckBox *screenReaderEnabledCheck_ = nullptr;
    QSpinBox *screenCaptureX_ = nullptr, *screenCaptureY_ = nullptr;
    QSpinBox *screenCaptureWidth_ = nullptr, *screenCaptureHeight_ = nullptr;
    QPushButton *useScreenPositionButton_ = nullptr;
    QDoubleSpinBox *currentYawInit_ = nullptr, *currentPitchInit_ = nullptr;
    QDoubleSpinBox *overlayFov_ = nullptr;
    QTimer *overlayTimer_ = nullptr;
    QTimer *screenCaptureTimer_ = nullptr;
    bspvision::ClShowPosReader screenReader_;
    bspvision::LiveOverlay *overlay_ = nullptr;
    QLocalServer *bridgeServer_ = nullptr;
    QLocalSocket *bridgeSocket_ = nullptr;
    QJsonObject bridgeTelemetry_;
    bool bridgeConnected_ = false;
    bool bridgePositionInitialized_ = false;
    qint64 lastBridgeCommandMs_ = 0;
    QString loadedMapName_;
    int pickMode_ = 0;
    bool targetDefined_ = false;
    bspvision::BspFile bspFile_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
