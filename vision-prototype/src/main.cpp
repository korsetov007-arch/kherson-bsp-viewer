#include "app_language.hpp"
#include "bsp_file.hpp"
#include "bsp_map_view.hpp"
#include "bsp_3d_view.hpp"
#include "ballistics_view.hpp"

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
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSaveFile>
#include <QTimer>
#include <QFileDialog>
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
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <algorithm>
#include <cmath>
#include <limits>
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
        gmodDataPath_ = settings.value(QStringLiteral("gmodDataPath")).toString();
        createSettingsMenu();
        rebuildInterface();
        livePollTimer_ = new QTimer(this);
        connect(livePollTimer_, &QTimer::timeout, this, [this] { pollLiveGMod(); });
        livePollTimer_->start(100);
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
        const auto value = [](QDoubleSpinBox *box, double fallback) {
            return box ? box->value() : fallback;
        };
        const double ox = value(originX_, 0.0), oy = value(originY_, 0.0), oz = value(originZ_, 32.0);
        const double tx = value(targetX_, 512.0), ty = value(targetY_, 0.0), tz = value(targetZ_, 32.0);
        const double speed = value(speed_, 1000.0), gravity = value(gravity_, 800.0);
        const double yaw = value(yaw_, 0.0), pitch = value(pitch_, 25.0), flight = value(flightTime_, 2.0);
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
        showSkyboxCheck_ = nullptr;
        originX_ = originY_ = originZ_ = nullptr;
        targetX_ = targetY_ = targetZ_ = nullptr;
        speed_ = gravity_ = yaw_ = pitch_ = flightTime_ = nullptr;

        buildBspTab(tabs_);
        buildBallisticsTab(tabs_);
        originX_->setValue(ox); originY_->setValue(oy); originZ_->setValue(oz);
        targetX_->setValue(tx); targetY_->setValue(ty); targetZ_->setValue(tz);
        speed_->setValue(speed); gravity_->setValue(gravity);
        yaw_->setValue(yaw); pitch_->setValue(pitch); flightTime_->setValue(flight);

        updateSettingsMenuText();
        setWindowTitle(bspvision::uiText("Source BSP Geometry Lab", "Source BSP: Геометрия и баллистика"));
        if (bspFile_.isLoaded()) refreshMapViews();
        tabs_->setCurrentIndex(std::clamp(previousTab, 0, tabs_->count() - 1));
        if (hadTrajectory) calculateArc(false);
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
                pickMode_ = 0;
                statusBar()->showMessage(bspvision::uiText("Launch point set from map. Set launch Z manually.", "Точка запуска выбрана на карте. Укажите Z вручную."));
            } else if (pickMode_ == 2) {
                targetX_->setValue(x); targetY_->setValue(y);
                targetDefined_ = true;
                pickMode_ = 0;
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

        auto *liveGroup = new QGroupBox(bspvision::uiText("Garry's Mod live link", "Связь с Garry's Mod в реальном времени"), tab);
        auto *liveLayout = new QVBoxLayout(liveGroup);
        auto *liveActions = new QHBoxLayout();
        auto *chooseGmodData = new QPushButton(bspvision::uiText(
            "Select garrysmod/data folder…", "Выбрать папку garrysmod/data…"), liveGroup);
        liveActions->addWidget(chooseGmodData);
        liveStatusLabel_ = new QLabel(bspvision::uiText(
            "Not connected. Select the Garry's Mod garrysmod/data folder and install the client addon.",
            "Не подключено. Выберите папку garrysmod/data и установите клиентский аддон."), liveGroup);
        liveStatusLabel_->setWordWrap(true);
        liveActions->addWidget(liveStatusLabel_, 1);
        liveLayout->addLayout(liveActions);
        liveTelemetryLabel_ = new QLabel(liveGroup);
        liveTelemetryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        liveTelemetryLabel_->setWordWrap(true);
        liveLayout->addWidget(liveTelemetryLabel_);
        layout->addWidget(liveGroup);
        connect(chooseGmodData, &QPushButton::clicked, this, [this] { chooseGmodDataFolder(); });

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
            writeAimState(false, QStringLiteral("No target selected"));
            ballisticsSummary_->setText(bspvision::uiText("Trajectory and live aim target cleared.", "Траектория и цель для HUD очищены."));
        });
    }

    void calculateArc(bool solveTarget, bool highArc = false) {
        const double ox = originX_->value(), oy = originY_->value(), oz = originZ_->value();
        const double tx = targetX_->value(), ty = targetY_->value(), tz = targetZ_->value();
        const double speed = speed_->value(), g = gravity_->value();
        double yaw = qDegreesToRadians(yaw_->value());
        double pitch = qDegreesToRadians(pitch_->value());
        double duration = flightTime_->value();
        targetDefined_ = targetDefined_ || solveTarget;
        if (solveTarget) highArcSelected_ = highArc;

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
        summary += bspvision::uiText(" Green = launch, red = target, amber = trajectory. When the GMod link is connected, the client HUD can show the calculated angle.", " Зелёный = запуск, красный = цель, янтарный = траектория. При подключении к GMod клиентский HUD показывает рассчитанный угол.");
        ballisticsSummary_->setText(summary);
    }

    void chooseGmodDataFolder() {
        const QString selected = QFileDialog::getExistingDirectory(
            this,
            bspvision::uiText("Select Garry's Mod garrysmod/data folder",
                              "Выберите папку Garry's Mod garrysmod/data"),
            gmodDataPath_);
        if (selected.isEmpty()) return;

        gmodDataPath_ = QDir(selected).absolutePath();
        QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
        settings.setValue(QStringLiteral("gmodDataPath"), gmodDataPath_);
        QDir().mkpath(QDir(gmodDataPath_).filePath(QStringLiteral("source_bsp_geometry_lab")));
        liveStatusMessage_ = bspvision::uiText(
            "Data folder saved. Waiting for the client addon telemetry…",
            "Папка сохранена. Ожидаем телеметрию клиентского аддона…");
        updateLiveLabels();
        pollLiveGMod();
    }

    void updateLiveLabels() {
        if (liveStatusLabel_) liveStatusLabel_->setText(liveStatusMessage_);
        if (liveTelemetryLabel_) liveTelemetryLabel_->setText(liveTelemetryMessage_);
    }

    void writeAimState(bool active, const QString &reason = {}) {
        if (gmodDataPath_.isEmpty()) return;
        const QString exchangeDir = QDir(gmodDataPath_).filePath(QStringLiteral("source_bsp_geometry_lab"));
        if (!QDir().mkpath(exchangeDir)) return;

        QJsonObject state;
        state.insert(QStringLiteral("version"), 1);
        state.insert(QStringLiteral("timestamp"), QDateTime::currentSecsSinceEpoch());
        state.insert(QStringLiteral("active"), active);
        state.insert(QStringLiteral("reason"), reason);
        state.insert(QStringLiteral("map"), liveMapName_);
        if (active) {
            QJsonObject target;
            target.insert(QStringLiteral("x"), targetX_ ? targetX_->value() : 0.0);
            target.insert(QStringLiteral("y"), targetY_ ? targetY_->value() : 0.0);
            target.insert(QStringLiteral("z"), targetZ_ ? targetZ_->value() : 0.0);
            state.insert(QStringLiteral("target"), target);
            state.insert(QStringLiteral("desiredYaw"), yaw_ ? yaw_->value() : 0.0);
            // Source/GMod view pitch is positive downward; ballistic elevation is positive upward.
            state.insert(QStringLiteral("desiredPitch"), pitch_ ? -pitch_->value() : 0.0);
            state.insert(QStringLiteral("yawDeg"), yaw_ ? yaw_->value() : 0.0);
            state.insert(QStringLiteral("elevationDeg"), pitch_ ? pitch_->value() : 0.0);
            state.insert(QStringLiteral("range"), liveRange_);
            state.insert(QStringLiteral("flightSeconds"), flightTime_ ? flightTime_->value() : 0.0);
        }

        QSaveFile file(QDir(exchangeDir).filePath(QStringLiteral("aim.json")));
        if (!file.open(QIODevice::WriteOnly)) return;
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }

    void pollLiveGMod() {
        if (gmodDataPath_.isEmpty()) {
            liveConnected_ = false;
            liveStatusMessage_ = bspvision::uiText(
                "Not connected. Select the Garry's Mod garrysmod/data folder.",
                "Не подключено. Выберите папку Garry's Mod garrysmod/data.");
            liveTelemetryMessage_.clear();
            updateLiveLabels();
            return;
        }

        const QString exchangeDir = QDir(gmodDataPath_).filePath(QStringLiteral("source_bsp_geometry_lab"));
        const QString telemetryPath = QDir(exchangeDir).filePath(QStringLiteral("telemetry.json"));
        QFileInfo telemetryInfo(telemetryPath);
        const qint64 ageMs = telemetryInfo.exists()
            ? QDateTime::currentDateTime().toMSecsSinceEpoch() - telemetryInfo.lastModified().toMSecsSinceEpoch()
            : std::numeric_limits<qint64>::max();
        if (!telemetryInfo.exists() || ageMs < 0 || ageMs > 2500) {
            liveConnected_ = false;
            liveStatusMessage_ = bspvision::uiText(
                "Waiting for telemetry. Install the client addon, start Garry's Mod, and check the selected data folder.",
                "Ожидаем телеметрию. Установите клиентский аддон, запустите Garry's Mod и проверьте выбранную папку.");
            liveTelemetryMessage_.clear();
            updateLiveLabels();
            writeAimState(false, QStringLiteral("Telemetry unavailable"));
            return;
        }

        QFile file(telemetryPath);
        if (!file.open(QIODevice::ReadOnly)) return;
        QJsonParseError parseError;
        const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
        file.close();
        if (parseError.error != QJsonParseError::NoError || !document.isObject()) return;
        const QJsonObject data = document.object();
        const QJsonObject angles = data.value(QStringLiteral("angles")).toObject();
        QJsonObject shootPos = data.value(QStringLiteral("shootPos")).toObject();
        if (!shootPos.contains(QStringLiteral("x"))) shootPos = data.value(QStringLiteral("eyePos")).toObject();
        if (!shootPos.contains(QStringLiteral("x"))) shootPos = data.value(QStringLiteral("playerPos")).toObject();
        if (!shootPos.contains(QStringLiteral("x"))) {
            writeAimState(false, QStringLiteral("Telemetry has no position"));
            return;
        }

        liveConnected_ = true;
        liveMapName_ = data.value(QStringLiteral("map")).toString();
        liveX_ = shootPos.value(QStringLiteral("x")).toDouble();
        liveY_ = shootPos.value(QStringLiteral("y")).toDouble();
        liveZ_ = shootPos.value(QStringLiteral("z")).toDouble();
        liveYaw_ = angles.value(QStringLiteral("yaw")).toDouble();
        livePitch_ = angles.value(QStringLiteral("pitch")).toDouble();

        if (originX_) originX_->setValue(liveX_);
        if (originY_) originY_->setValue(liveY_);
        if (originZ_) originZ_->setValue(liveZ_);

        liveTelemetryMessage_ = bspvision::uiText(
            "Map: %1 · launch XYZ: %2, %3, %4 · camera yaw/pitch: %5° / %6°",
            "Карта: %1 · начало XYZ: %2, %3, %4 · углы камеры yaw/pitch: %5° / %6°")
            .arg(liveMapName_.isEmpty() ? QStringLiteral("—") : liveMapName_)
            .arg(liveX_, 0, 'f', 1).arg(liveY_, 0, 'f', 1).arg(liveZ_, 0, 'f', 1)
            .arg(liveYaw_, 0, 'f', 1).arg(livePitch_, 0, 'f', 1);

        if (loadedMapName_.isEmpty() ||
            liveMapName_.compare(loadedMapName_, Qt::CaseInsensitive) != 0) {
            liveStatusMessage_ = bspvision::uiText(
                "Connected, but the loaded BSP must match the current game map. BSP: %1 · game: %2",
                "Подключено, но загруженная BSP-карта должна совпадать с текущей картой игры. BSP: %1 · игра: %2")
                .arg(loadedMapName_.isEmpty() ? QStringLiteral("—") : loadedMapName_)
                .arg(liveMapName_.isEmpty() ? QStringLiteral("—") : liveMapName_);
            writeAimState(false, QStringLiteral("BSP/game map mismatch"));
        } else if (!targetDefined_) {
            liveStatusMessage_ = bspvision::uiText(
                "Connected. Choose a target on the BSP map, set its Z height, then solve for a low/high arc.",
                "Подключено. Выберите цель на BSP-карте, задайте её высоту Z и рассчитайте низкую/высокую дугу.");
            writeAimState(false, QStringLiteral("No target selected"));
        } else {
            const double dx = targetX_->value() - liveX_;
            const double dy = targetY_->value() - liveY_;
            const double dz = targetZ_->value() - liveZ_;
            const double range = std::hypot(dx, dy);
            const double speed = speed_->value();
            const double gravity = gravity_->value();
            QString solveError;
            double yawRad = std::atan2(dy, dx);
            double pitchRad = 0.0;
            double duration = 0.0;
            bool solved = true;

            if (range < 0.01) {
                solved = false;
                solveError = bspvision::uiText("Target is too close to the launch point.", "Цель слишком близко к точке запуска.");
            } else if (speed <= 0.01) {
                solved = false;
                solveError = bspvision::uiText("Projectile speed must be positive.", "Скорость снаряда должна быть положительной.");
            } else if (gravity <= 0.0) {
                pitchRad = std::atan2(dz, range);
            } else {
                const double v2 = speed * speed;
                const double discriminant = v2 * v2 - gravity * (gravity * range * range + 2.0 * dz * v2);
                if (discriminant < 0.0) {
                    solved = false;
                    solveError = bspvision::uiText(
                        "Target is outside the ideal range for these speed/gravity values.",
                        "Цель недостижима при заданных скорости и гравитации.");
                } else {
                    const double root = std::sqrt(discriminant);
                    const double tangent = (v2 + (highArcSelected_ ? root : -root)) / (gravity * range);
                    pitchRad = std::atan(tangent);
                }
            }

            if (solved) {
                const double horizontalSpeed = speed * std::cos(pitchRad);
                if (horizontalSpeed <= 0.01) {
                    solved = false;
                    solveError = bspvision::uiText("Computed arc is too steep.", "Рассчитанная дуга слишком крутая.");
                } else {
                    duration = range / horizontalSpeed;
                }
            }

            if (!solved) {
                liveRange_ = range;
                liveStatusMessage_ = bspvision::uiText("Connected, but no solution: %1", "Подключено, но решения нет: %1").arg(solveError);
                writeAimState(false, solveError);
            } else {
                liveRange_ = range;
                yaw_->setValue(qRadiansToDegrees(yawRad));
                pitch_->setValue(qRadiansToDegrees(pitchRad));
                flightTime_->setValue(std::clamp(duration, 0.05, 30.0));
                calculateArc(false);
                liveStatusMessage_ = bspvision::uiText(
                    "Connected · live solution active · range %1 · flight %2 s",
                    "Подключено · расчёт активен · дальность %1 · полёт %2 с")
                    .arg(range, 0, 'f', 0).arg(duration, 0, 'f', 2);
                writeAimState(true);
            }
        }
        updateLiveLabels();
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
    QTimer *livePollTimer_ = nullptr;
    QString gmodDataPath_;
    QString liveStatusMessage_;
    QString liveTelemetryMessage_;
    QString liveMapName_;
    QString loadedMapName_;
    bool liveConnected_ = false;
    bool highArcSelected_ = false;
    double liveX_ = 0.0, liveY_ = 0.0, liveZ_ = 0.0;
    double liveYaw_ = 0.0, livePitch_ = 0.0, liveRange_ = 0.0;
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
