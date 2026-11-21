#include "app_language.hpp"
#include "bsp_file.hpp"
#include "bsp_map_view.hpp"
#include "bsp_3d_view.hpp"
#include "ballistics_view.hpp"
#include "live_overlay.hpp"
#include "raw_mouse_tracker.hpp"

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
#include <QCoreApplication>
#include <QClipboard>
#include <QFileDialog>
#include <QGuiApplication>
#include <QList>
#include <QRegularExpression>
#include <QScreen>
#include <QTimer>
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
#include <QVector3D>
#include <QtMath>

namespace {
quintptr nativeHandleValue(WId handle) {
#ifdef Q_OS_WIN
    return reinterpret_cast<quintptr>(handle);
#else
    return static_cast<quintptr>(handle);
#endif
}
}

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
        createSettingsMenu();
        rebuildInterface();
        overlay_ = new bspvision::LiveOverlay();
        rawInputReady_ = mouseTracker_.initialize(nativeHandleValue(winId()));
        mouseTracker_.anglesChanged = [this](double yaw, double pitch) {
            if (overlay_) overlay_->setCurrentAngles(yaw, pitch);
        };
        overlayTimer_ = new QTimer(this);
        connect(overlayTimer_, &QTimer::timeout, this, [this] { syncOverlay(); });
        overlayTimer_->start(16);
    }

public:
    ~MainWindow() override {
        if (QCoreApplication::instance())
            QCoreApplication::instance()->removeNativeEventFilter(&mouseTracker_);
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
        const auto value = [](QDoubleSpinBox *box, double fallback) {
            return box ? box->value() : fallback;
        };
        const double ox = value(originX_, 0.0), oy = value(originY_, 0.0), oz = value(originZ_, 32.0);
        const double tx = value(targetX_, 512.0), ty = value(targetY_, 0.0), tz = value(targetZ_, 32.0);
        const double speed = value(speed_, 1000.0), gravity = value(gravity_, 800.0);
        const double yaw = value(yaw_, 0.0), pitch = value(pitch_, 25.0), flight = value(flightTime_, 2.0);
        const double initialYaw = value(currentYawInit_, 0.0), initialPitch = value(currentPitchInit_, 0.0);
        const double sensitivity = value(sensitivity_, 3.0), mYaw = value(mYaw_, 0.022), mPitch = value(mPitch_, 0.022);
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
        showSkyboxCheck_ = nullptr;
        currentYawInit_ = currentPitchInit_ = sensitivity_ = mYaw_ = mPitch_ = overlayFov_ = nullptr;
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
        sensitivity_->setValue(sensitivity); mYaw_->setValue(mYaw); mPitch_->setValue(mPitch);
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
            "External overlay · no addon", "Внешний оверлей · без аддона"), tab);
        auto *liveLayout = new QVBoxLayout(liveGroup);
        currentYawInit_ = makeCoordinate(0.0, liveGroup);
        currentYawInit_->setRange(-180.0, 180.0);
        currentYawInit_->setSingleStep(5.0);
        currentPitchInit_ = makeCoordinate(0.0, liveGroup);
        currentPitchInit_->setRange(-89.0, 89.0);
        currentPitchInit_->setSingleStep(2.0);
        sensitivity_ = makeCoordinate(3.0, liveGroup);
        sensitivity_->setRange(0.01, 50.0);
        sensitivity_->setSingleStep(0.25);
        auto makeFineField = [liveGroup](double value) {
            auto *field = new QDoubleSpinBox(liveGroup);
            field->setRange(-1.0, 1.0);
            field->setDecimals(4);
            field->setSingleStep(0.001);
            field->setValue(value);
            field->setMinimumWidth(88);
            return field;
        };
        mYaw_ = makeFineField(0.022);
        mPitch_ = makeFineField(0.022);
        overlayFov_ = makeCoordinate(90.0, liveGroup);
        overlayFov_->setRange(30.0, 140.0);
        overlayFov_->setSingleStep(5.0);

        auto *calibrationRow1 = new QHBoxLayout();
        auto *calibrationRow2 = new QHBoxLayout();
        auto addCalibrationField = [liveGroup](QHBoxLayout *row, const QString &label, QDoubleSpinBox *field) {
            row->addWidget(new QLabel(label, liveGroup));
            row->addWidget(field);
        };
        addCalibrationField(calibrationRow1, bspvision::uiText("Initial yaw °", "Начальный yaw °"), currentYawInit_);
        addCalibrationField(calibrationRow1, bspvision::uiText("Initial pitch °", "Начальный pitch °"), currentPitchInit_);
        addCalibrationField(calibrationRow1, bspvision::uiText("Sensitivity", "Чувствительность"), sensitivity_);
        calibrationRow1->addStretch();
        addCalibrationField(calibrationRow2, QStringLiteral("m_yaw"), mYaw_);
        addCalibrationField(calibrationRow2, QStringLiteral("m_pitch"), mPitch_);
        addCalibrationField(calibrationRow2, bspvision::uiText("Horizontal FOV °", "Горизонтальный FOV °"), overlayFov_);
        auto *pasteGetpos = new QPushButton(bspvision::uiText("Paste getpos", "Вставить getpos"), liveGroup);
        auto *startOverlay = new QPushButton(bspvision::uiText("Start overlay", "Запустить оверлей"), liveGroup);
        auto *stopOverlay = new QPushButton(bspvision::uiText("Stop overlay", "Остановить оверлей"), liveGroup);
        calibrationRow2->addWidget(pasteGetpos);
        calibrationRow2->addWidget(startOverlay);
        calibrationRow2->addWidget(stopOverlay);
        liveLayout->addLayout(calibrationRow1);
        liveLayout->addLayout(calibrationRow2);

        liveStatusLabel_ = new QLabel(bspvision::uiText(
            "Run getpos in the game console, copy its output, then paste it here. Mouse deltas are tracked only while Garry's Mod is the foreground window.",
            "Выполните getpos в консоли игры, скопируйте вывод и вставьте сюда. Движения мыши учитываются, только когда Garry's Mod активен."), liveGroup);
        liveStatusLabel_->setWordWrap(true);
        liveLayout->addWidget(liveStatusLabel_);
        liveTelemetryLabel_ = new QLabel(liveGroup);
        liveTelemetryLabel_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        liveTelemetryLabel_->setWordWrap(true);
        liveLayout->addWidget(liveTelemetryLabel_);
        layout->addWidget(liveGroup);
        connect(pasteGetpos, &QPushButton::clicked, this, [this] { pasteGetposFromClipboard(); });
        connect(startOverlay, &QPushButton::clicked, this, [this] { startExternalOverlay(); });
        connect(stopOverlay, &QPushButton::clicked, this, [this] { stopExternalOverlay(); });
        auto calibrationChanged = [this] {
            mouseTracker_.setCalibration(sensitivity_->value(), mYaw_->value(), mPitch_->value());
            syncOverlay();
        };
        connect(sensitivity_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [calibrationChanged](double) { calibrationChanged(); });
        connect(mYaw_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [calibrationChanged](double) { calibrationChanged(); });
        connect(mPitch_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [calibrationChanged](double) { calibrationChanged(); });
        connect(overlayFov_, qOverload<double>(&QDoubleSpinBox::valueChanged), this, [this](double) { syncOverlay(); });

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

    void calculateArc(bool solveTarget, bool highArc = false) {
        const double ox = originX_->value(), oy = originY_->value(), oz = originZ_->value();
        const double tx = targetX_->value(), ty = targetY_->value(), tz = targetZ_->value();
        const double speed = speed_->value(), g = gravity_->value();
        double yaw = qDegreesToRadians(yaw_->value());
        double pitch = qDegreesToRadians(pitch_->value());
        double duration = flightTime_->value();
        trajectoryCalculated_ = false;
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
        summary += bspvision::uiText(" Green = launch, red = target, amber = trajectory. The external overlay estimates view angles from raw mouse input.", " Зелёный = запуск, красный = цель, янтарный = траектория. Внешний оверлей оценивает углы по сырым данным мыши.");
        ballisticsSummary_->setText(summary);
        syncOverlay();
    }

    void pasteGetposFromClipboard() {
        const QString clipboard = QApplication::clipboard()->text();
        if (clipboard.trimmed().isEmpty()) {
            QMessageBox::information(this, bspvision::uiText("Paste getpos", "Вставка getpos"),
                bspvision::uiText("Clipboard is empty. Run getpos in the game console and copy its output first.",
                                  "Буфер обмена пуст. Сначала выполните getpos в консоли игры и скопируйте вывод."));
            return;
        }
        const QString number = QStringLiteral("([+-]?(?:[0-9]+(?:\\.[0-9]*)?|\\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
        const QRegularExpression positionRe(
            QStringLiteral("\\bsetpos(?:_exact)?\\s+") + number + QStringLiteral("\\s+") +
            number + QStringLiteral("\\s+") + number, QRegularExpression::CaseInsensitiveOption);
        const QRegularExpression angleRe(
            QStringLiteral("\\bsetang\\s+") + number + QStringLiteral("\\s+") +
            number + QStringLiteral("\\s+") + number, QRegularExpression::CaseInsensitiveOption);
        const auto positionMatch = positionRe.match(clipboard);
        const auto angleMatch = angleRe.match(clipboard);
        bool gotAnything = false;
        if (positionMatch.hasMatch()) {
            trajectoryCalculated_ = false;
            originX_->setValue(positionMatch.captured(1).toDouble());
            originY_->setValue(positionMatch.captured(2).toDouble());
            originZ_->setValue(positionMatch.captured(3).toDouble());
            gotAnything = true;
        }
        if (angleMatch.hasMatch()) {
            currentPitchInit_->setValue(angleMatch.captured(1).toDouble());
            currentYawInit_->setValue(angleMatch.captured(2).toDouble());
            gotAnything = true;
        }
        if (!gotAnything) {
            QMessageBox::warning(this, bspvision::uiText("Could not parse getpos", "Не удалось разобрать getpos"),
                bspvision::uiText("Expected text similar to: setpos X Y Z; setang PITCH YAW ROLL.",
                                  "Ожидался текст вида: setpos X Y Z; setang PITCH YAW ROLL."));
            return;
        }
        if (mouseTracker_.enabled()) {
            mouseTracker_.start(currentYawInit_->value(), currentPitchInit_->value(),
                                sensitivity_->value(), mYaw_->value(), mPitch_->value());
        }
        liveStatusLabel_->setText(bspvision::uiText(
            "getpos loaded. Origin and initial view angles were filled where present. If the overlay is running, its angle estimate was re-anchored.",
            "getpos загружен. Координаты и исходные углы заполнены. Если оверлей работает, его оценка углов сброшена на новые значения."));
        syncOverlay();
    }

    void startExternalOverlay() {
        if (!rawInputReady_) rawInputReady_ = mouseTracker_.initialize(nativeHandleValue(winId()));
        if (!rawInputReady_) {
            QMessageBox::warning(this, bspvision::uiText("Raw mouse input unavailable", "Недоступен Raw Input"),
                mouseTracker_.lastError().isEmpty()
                    ? bspvision::uiText("Could not initialize Windows raw mouse input.", "Не удалось включить Windows Raw Input.")
                    : mouseTracker_.lastError());
            return;
        }
        mouseTracker_.start(currentYawInit_->value(), currentPitchInit_->value(),
                            sensitivity_->value(), mYaw_->value(), mPitch_->value());
        QScreen *targetScreen = screen();
        if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
        if (targetScreen) overlay_->showOverlayOn(targetScreen->geometry());
        overlay_->setTargetEnabled(targetDefined_ && trajectoryCalculated_);
        liveStatusLabel_->setText(bspvision::uiText(
            "Overlay is on. Switch to Garry's Mod; tracking updates only while the game is foreground. Borderless fullscreen is recommended.",
            "Оверлей включён. Переключитесь в Garry's Mod: углы обновляются, только когда игра на переднем плане. Рекомендуется безрамочный полноэкранный режим."));
        syncOverlay();
    }

    void stopExternalOverlay() {
        mouseTracker_.stop();
        if (overlay_) overlay_->hide();
        liveStatusLabel_->setText(bspvision::uiText("Overlay stopped.", "Оверлей остановлен."));
        syncOverlay();
    }

    void syncOverlay() {
        if (!overlay_) return;
        mouseTracker_.refreshForegroundState();
        overlay_->setCurrentAngles(mouseTracker_.yawDeg(), mouseTracker_.sourcePitchDeg());
        overlay_->setDesiredAngles(yaw_ ? yaw_->value() : 0.0, pitch_ ? -pitch_->value() : 0.0);
        overlay_->setHorizontalFov(overlayFov_ ? overlayFov_->value() : 90.0);
        const double range = (originX_ && targetX_)
            ? std::hypot(targetX_->value() - originX_->value(), targetY_->value() - originY_->value())
            : 0.0;
        overlay_->setTargetDetails(range, flightTime_ ? flightTime_->value() : 0.0, loadedMapName_);
        overlay_->setTargetEnabled(mouseTracker_.enabled() && mouseTracker_.gameForeground()
                                  && targetDefined_ && trajectoryCalculated_);
        QString state;
        if (!mouseTracker_.enabled()) {
            state = bspvision::uiText("Tracking stopped", "Отслеживание остановлено");
        } else if (mouseTracker_.gameForeground()) {
            state = bspvision::uiText("GMod focused", "Игра активна");
        } else {
            state = bspvision::uiText("Waiting for GMod focus", "Ожидаем фокус игры");
        }
        if (liveTelemetryLabel_) {
            liveTelemetryLabel_->setText(bspvision::uiText(
                "Estimated yaw %1° · pitch %2° · %3",
                "Расчётные yaw %1° · pitch %2° · %3")
                .arg(mouseTracker_.yawDeg(), 0, 'f', 2)
                .arg(mouseTracker_.sourcePitchDeg(), 0, 'f', 2)
                .arg(state));
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
    QDoubleSpinBox *currentYawInit_ = nullptr, *currentPitchInit_ = nullptr;
    QDoubleSpinBox *sensitivity_ = nullptr, *mYaw_ = nullptr, *mPitch_ = nullptr, *overlayFov_ = nullptr;
    QTimer *overlayTimer_ = nullptr;
    bspvision::LiveOverlay *overlay_ = nullptr;
    bspvision::RawMouseTracker mouseTracker_;
    bool rawInputReady_ = false;
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
