#include "app_language.hpp"
#include "bsp_file.hpp"
#include "bsp_map_view.hpp"
#include "bsp_3d_view.hpp"
#include "ballistics_view.hpp"
#include "live_overlay.hpp"
#include "cl_showpos_reader.hpp"
#include "heightfield.hpp"
#include "triangle_bvh.hpp"

#include <QAbstractItemView>
#include <QAction>
#include <QActionGroup>
#include <QCheckBox>
#include <QCloseEvent>
#include <QComboBox>
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
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHeaderView>
#include <QLabel>
#include <QMainWindow>
#include <QPainter>
#include <QKeyEvent>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPushButton>
#include <QStatusBar>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#ifdef Q_OS_WIN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <algorithm>
#include <cmath>
#include <functional>
#include <QVector3D>
#include <QtMath>


class ScreenRegionSelector final : public QWidget {
public:
    explicit ScreenRegionSelector(QScreen *screen, QWidget *parent = nullptr)
        : QWidget(parent, Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint),
          screen_(screen) {
        if (screen_) setGeometry(screen_->geometry());
        setAttribute(Qt::WA_TranslucentBackground);
        setAttribute(Qt::WA_DeleteOnClose);
        setMouseTracking(true);
        setFocusPolicy(Qt::StrongFocus);
        setCursor(Qt::CrossCursor);
    }

    std::function<void(const QRect &)> regionSelected;
    std::function<void()> canceled;

protected:
    void paintEvent(QPaintEvent *event) override {
        Q_UNUSED(event);
        QPainter painter(this);
        painter.fillRect(rect(), QColor(8, 12, 18, 92));
        painter.setPen(QColor(245, 248, 252));
        painter.setBrush(QColor(20, 27, 38, 220));
        const QRect banner(24, 22, std::min(width() - 48, 650), 58);
        painter.drawRoundedRect(banner, 8, 8);
        painter.drawText(banner.adjusted(14, 5, -14, -5),
                         Qt::AlignVCenter | Qt::AlignLeft | Qt::TextWordWrap,
                         message_.isEmpty()
                             ? bspvision::uiText(
                                   "Drag a rectangle around cl_showpos pos:/ang: (and vel:) lines. Esc or right-click cancels.",
                                   "Обведите рамкой строки cl_showpos pos:/ang: (и vel:). Esc или правая кнопка — отмена.")
                             : message_);

        if (dragging_) {
            const QRect selection = currentRect();
            painter.setPen(QPen(QColor(95, 255, 188), 2, Qt::SolidLine));
            painter.setBrush(QColor(76, 230, 170, 42));
            painter.drawRect(selection);
            painter.setPen(QColor(245, 248, 252));
            painter.setBrush(QColor(20, 27, 38, 220));
            const qreal ratio = screen_ ? screen_->devicePixelRatio() : 1.0;
            const QString sizeText = QStringLiteral("%1 × %2 physical px")
                .arg(qRound(selection.width() * ratio)).arg(qRound(selection.height() * ratio));
            QRect label(selection.left(), std::max(92, selection.top() - 28), 140, 24);
            if (label.right() > width()) label.moveRight(width() - 8);
            painter.drawRoundedRect(label, 4, 4);
            painter.drawText(label, Qt::AlignCenter, sizeText);
        }
    }

    void mousePressEvent(QMouseEvent *event) override {
        if (event->button() == Qt::LeftButton) {
            anchor_ = event->position().toPoint();
            cursorPosition_ = anchor_;
            dragging_ = true;
            message_.clear();
            update();
            event->accept();
            return;
        }
        if (event->button() == Qt::RightButton) {
            cancelSelection();
            event->accept();
            return;
        }
        QWidget::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent *event) override {
        if (dragging_) {
            cursorPosition_ = event->position().toPoint();
            update();
            event->accept();
            return;
        }
        QWidget::mouseMoveEvent(event);
    }

    void mouseReleaseEvent(QMouseEvent *event) override {
        if (event->button() != Qt::LeftButton || !dragging_) {
            QWidget::mouseReleaseEvent(event);
            return;
        }
        cursorPosition_ = event->position().toPoint();
        dragging_ = false;
        const QRect selected = currentRect().intersected(rect());
        const qreal ratio = screen_ ? screen_->devicePixelRatio() : 1.0;
        const QRect physicalPixels(
            qRound(selected.x() * ratio), qRound(selected.y() * ratio),
            qRound(selected.width() * ratio), qRound(selected.height() * ratio));
        if (physicalPixels.width() < 200 || physicalPixels.height() < 45) {
            message_ = bspvision::uiText(
                "Crop too small. Select at least 200 × 45 physical pixels around the text; Esc cancels.",
                "Область слишком мала. Выделите не менее 200 × 45 физических пикселей вокруг текста; Esc — отмена.");
            update();
            event->accept();
            return;
        }

        finished_ = true;
        if (regionSelected) regionSelected(physicalPixels);
        close();
        event->accept();
    }

    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_Escape) {
            cancelSelection();
            event->accept();
            return;
        }
        QWidget::keyPressEvent(event);
    }

    void closeEvent(QCloseEvent *event) override {
        if (!finished_) {
            finished_ = true;
            const auto notifyCancelled = canceled;
            if (notifyCancelled) {
                QTimer::singleShot(0, parentWidget(), [notifyCancelled] { notifyCancelled(); });
            }
        }
        QWidget::closeEvent(event);
    }

private:
    QRect currentRect() const {
        return QRect(anchor_, cursorPosition_).normalized();
    }

    void cancelSelection() {
        if (finished_) return;
        finished_ = true;
        close();
        if (canceled) canceled();
    }

    QScreen *screen_ = nullptr;
    QPoint anchor_;
    QPoint cursorPosition_;
    bool dragging_ = false;
    bool finished_ = false;
    QString message_;
};

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
        overlayTimer_ = new QTimer(this);
        connect(overlayTimer_, &QTimer::timeout, this, [this] { syncOverlay(); });
        overlayTimer_->start(16);
#ifdef Q_OS_WIN
        hotkeyRegistered_ = RegisterHotKey(
            reinterpret_cast<HWND>(winId()), AutoAimHotkeyId, MOD_NOREPEAT, VK_F7) != FALSE;
        if (!hotkeyRegistered_) {
            statusBar()->showMessage(bspvision::uiText(
                "Could not register global F7 (another app may own it); F7 works while this window is focused.",
                "Не удалось зарегистрировать глобальную F7 (клавиша занята); F7 будет работать, когда активно это окно."));
        }
#endif
    }

#if defined(Q_OS_WIN)
    bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result) override {
        MSG *msg = static_cast<MSG *>(message);
        if (msg && msg->message == WM_HOTKEY &&
            msg->wParam == static_cast<WPARAM>(AutoAimHotkeyId)) {
            toggleAutoAim();
            if (result) *result = 0;
            return true;
        }
        return QMainWindow::nativeEvent(eventType, message, result);
    }
#endif

protected:
    void keyPressEvent(QKeyEvent *event) override {
        if (event->key() == Qt::Key_F7 && !event->isAutoRepeat()) {
#if defined(Q_OS_WIN)
            if (!hotkeyRegistered_) toggleAutoAim();
#else
            toggleAutoAim();
#endif
            event->accept();
            return;
        }
        QMainWindow::keyPressEvent(event);
    }

public:
    ~MainWindow() override {
#ifdef Q_OS_WIN
        if (hotkeyRegistered_)
            UnregisterHotKey(reinterpret_cast<HWND>(winId()), AutoAimHotkeyId);
#endif
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
        const double speed = value(speed_, 4000.0), gravity = value(gravity_, 600.0);
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
        cropRegionLabel_ = nullptr;
        screenReaderEnabledCheck_ = nullptr;
        screenCaptureX_ = screenCaptureY_ = screenCaptureWidth_ = screenCaptureHeight_ = nullptr;
        useScreenPositionButton_ = nullptr;
        showSkyboxCheck_ = nullptr;
        heightfieldCheck_ = nullptr;
        contourIntervalCombo_ = nullptr;
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
        if (heightfield_.isValid()) {
            mapView_->setHeightfield(heightfield_.hillshadeImage(), heightfield_.worldBounds());
            mapView_->setHeightfieldContours(heightfieldContourInterval_ == 256
                ? heightfield_.contours256() : heightfield_.contours128());
            mapView_->setHeightfieldVisible(heightfieldVisible_);
        } else {
            mapView_->setHeightfield({}, {});
            mapView_->setHeightfieldContours({});
            mapView_->setHeightfieldVisible(false);
        }
        meshView_->setMap(bspFile_.vertices(), polygons);
        ballisticsView_->setMap(bspFile_.vertices(), polygons);
        bspSummary_->setText(bspFile_.summary());
        populateLumpTable();
        if (showSkyboxCheck_) showSkyboxCheck_->setEnabled(true);
        if (heightfieldCheck_) heightfieldCheck_->setEnabled(true);
        if (contourIntervalCombo_) contourIntervalCombo_->setEnabled(true);
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

    void setHeightfieldVisible(bool visible) {
        if (!visible) {
            heightfieldVisible_ = false;
            if (mapView_) mapView_->setHeightfieldVisible(false);
            return;
        }
        if (!bspFile_.isLoaded()) return;

        if (!heightfield_.isValid()) {
            statusBar()->showMessage(bspvision::uiText(
                "Building heightfield and hillshade…", "Строю heightfield и hillshade…"));
            QApplication::processEvents();
            QString error;
            const auto polygons = bspFile_.facePolygons(false);
            if (!heightfield_.build(polygons, 16.0, 2048, &error)) {
                heightfieldVisible_ = false;
                if (heightfieldCheck_) {
                    const QSignalBlocker blocker(heightfieldCheck_);
                    heightfieldCheck_->setChecked(false);
                }
                QMessageBox::warning(this, bspvision::uiText(
                    "Heightfield build failed", "Не удалось построить heightfield"), error);
                return;
            }
        }

        heightfieldVisible_ = true;
        if (mapView_) {
            mapView_->setHeightfield(heightfield_.hillshadeImage(), heightfield_.worldBounds());
            mapView_->setHeightfieldContours(heightfieldContourInterval_ == 256
                ? heightfield_.contours256() : heightfield_.contours128());
            mapView_->setHeightfieldVisible(true);
        }
        statusBar()->showMessage(bspvision::uiText(
            "Heightfield ready: %1×%2 cells, %3 covered, cell %4 units, contours %5.",
            "Heightfield готов: %1×%2 ячеек, покрыто %3, размер ячейки %4 ед., изолинии %5.")
            .arg(heightfield_.width()).arg(heightfield_.height())
            .arg(heightfield_.coveredCellCount()).arg(heightfield_.cellSize(), 0, 'f', 2)
            .arg(heightfieldContourInterval_));
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

        heightfieldCheck_ = new QCheckBox(
            bspvision::uiText("Hillshade heightfield", "Рельеф: heightfield / hillshade"), tab);
        heightfieldCheck_->setChecked(heightfieldVisible_);
        heightfieldCheck_->setEnabled(bspFile_.isLoaded());
        controls->addWidget(heightfieldCheck_);
        auto *contourLabel = new QLabel(bspvision::uiText("Contours", "Изолинии"), tab);
        controls->addWidget(contourLabel);
        contourIntervalCombo_ = new QComboBox(tab);
        contourIntervalCombo_->addItem(QStringLiteral("128 u"), 128);
        contourIntervalCombo_->addItem(QStringLiteral("256 u"), 256);
        contourIntervalCombo_->setCurrentIndex(heightfieldContourInterval_ == 256 ? 1 : 0);
        contourIntervalCombo_->setEnabled(bspFile_.isLoaded());
        contourIntervalCombo_->setToolTip(bspvision::uiText(
            "Contour interval in Source world units.", "Шаг изолиний в единицах мира Source."));
        controls->addWidget(contourIntervalCombo_);

        controls->addStretch();
        layout->addLayout(controls);
        connect(showSkyboxCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            showSkybox_ = checked;
            if (bspFile_.isLoaded()) refreshMapViews();
        });
        connect(heightfieldCheck_, &QCheckBox::toggled, this, [this](bool checked) {
            setHeightfieldVisible(checked);
        });
        connect(contourIntervalCombo_, qOverload<int>(&QComboBox::currentIndexChanged),
                this, [this](int index) {
            heightfieldContourInterval_ = (contourIntervalCombo_ && contourIntervalCombo_->itemData(index).toInt() == 256)
                ? 256 : 128;
            if (heightfield_.isValid() && mapView_) {
                mapView_->setHeightfieldContours(heightfieldContourInterval_ == 256
                    ? heightfield_.contours256() : heightfield_.contours128());
            }
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
            heightfield_.clear();
            heightfieldVisible_ = false;
            collisionBvh_.clear();
            collisionBvhReady_ = false;
            collisionBvhAttempted_ = false;
            collisionBvhError_.clear();
            if (heightfieldCheck_) {
                const QSignalBlocker blocker(heightfieldCheck_);
                heightfieldCheck_->setChecked(false);
                heightfieldCheck_->setEnabled(true);
            }
            if (contourIntervalCombo_) contourIntervalCombo_->setEnabled(true);
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
                updateAutoSolveControls();
                calculateArcAutomatically();
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
            "External overlay · cl_showpos tracking", "Внешний оверлей · отслеживание cl_showpos"), tab);
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
        auto *startOverlay = new QPushButton(bspvision::uiText("Show overlay", "Показать оверлей"), liveGroup);
        auto *stopOverlay = new QPushButton(bspvision::uiText("Hide overlay", "Скрыть оверлей"), liveGroup);
        fovRow->addWidget(startOverlay);
        fovRow->addWidget(stopOverlay);
        fovRow->addStretch();
        liveLayout->addLayout(fovRow);
        liveStatusLabel_ = new QLabel(bspvision::uiText(
            "F7 enables closed-loop ballistic auto aim in the foreground Garry's Mod window. It calibrates mouse-to-angle response from cl_showpos; F7 again immediately restores manual control.",
            "F7 включает автодоводку по рассчитанной траектории в активном окне Garry's Mod. Программа калибрует связь мыши с углами по cl_showpos; повторное F7 возвращает ручное управление."), liveGroup);
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
            bspvision::uiText("cl_showpos screen reader", "Экранное чтение cl_showpos"), tab);
        auto *screenLayout = new QVBoxLayout(screenGroup);
        QSettings screenSettings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
        QScreen *primary = QGuiApplication::primaryScreen();
        const QSize primarySize = primary ? primary->size() : QSize(1920, 1080);

        screenReaderEnabledCheck_ = new QCheckBox(
            bspvision::uiText("Enable screen reader", "Включить чтение с экрана"), screenGroup);
        screenReaderEnabledCheck_->setChecked(screenSettings.value(QStringLiteral("clShowPos/enabled"), false).toBool());
        screenLayout->addWidget(screenReaderEnabledCheck_);

        auto *selectCropButton = new QPushButton(
            bspvision::uiText("Select crop with mouse", "Выделить область мышью"), screenGroup);
        screenLayout->addWidget(selectCropButton);
        cropRegionLabel_ = new QLabel(screenGroup);
        cropRegionLabel_->setWordWrap(true);
        screenLayout->addWidget(cropRegionLabel_);

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
                "Enable cl_showpos 1 and drag a rectangle around the visible pos:/ang: rows (vel: is optional). Select again any time to adjust the crop. Borderless/windowed game mode works best.",
                "Включите cl_showpos 1 и выделите рамкой видимые строки pos:/ang: (vel: необязательна). Выделять заново можно в любой момент. Лучше работает оконный режим или безрамочное окно."), screenGroup);
        screenReaderStatusLabel_->setWordWrap(true);
        screenLayout->addWidget(screenReaderStatusLabel_);

        useScreenPositionButton_ = new QPushButton(
            bspvision::uiText("Use last pos as launch point", "Использовать pos как точку запуска"), screenGroup);
        useScreenPositionButton_->setEnabled(false);
        screenLayout->addWidget(useScreenPositionButton_);
        auto *screenNote = new QLabel(
            bspvision::uiText(
                "Classic pixel-template matching; no OCR engine, neural network or game module is used. pos is Source MainViewOrigin and may differ from the weapon muzzle.",
                "Распознавание пиксельными шаблонами; без OCR, нейросети и игровых модулей. pos — это MainViewOrigin Source, он может отличаться от дульного среза."), screenGroup);
        screenNote->setWordWrap(true);
        screenLayout->addWidget(screenNote);
        layout->addWidget(screenGroup);

        const auto persistScreenCrop = [this] {
            QSettings settings(QStringLiteral("SourceBSPTools"), QStringLiteral("GeometryLab"));
            if (screenCaptureX_) settings.setValue(QStringLiteral("clShowPos/x"), screenCaptureX_->value());
            if (screenCaptureY_) settings.setValue(QStringLiteral("clShowPos/y"), screenCaptureY_->value());
            if (screenCaptureWidth_) settings.setValue(QStringLiteral("clShowPos/width"), screenCaptureWidth_->value());
            if (screenCaptureHeight_) settings.setValue(QStringLiteral("clShowPos/height"), screenCaptureHeight_->value());
            updateCropRegionLabel();
        };
        updateCropRegionLabel();
        connect(selectCropButton, &QPushButton::clicked, this, [this] { beginCropSelection(); });
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
            bspvision::ClShowPosSample sample;
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
        speed_ = makeCoordinate(4000.0, physicsGroup);
        speed_->setRange(1.0, 100000.0);
        speed_->setSingleStep(50.0);
        gravity_ = makeCoordinate(600.0, physicsGroup);
        gravity_->setRange(0.0, 100000.0);
        gravity_->setSingleStep(50.0);
        yaw_ = makeCoordinate(0.0, physicsGroup);
        yaw_->setRange(-360.0, 360.0);
        yaw_->setSingleStep(5.0);
        pitch_ = makeCoordinate(25.0, physicsGroup);
        pitch_->setRange(-89.0, 89.0);
        pitch_->setSingleStep(1.0);
        flightTime_ = makeCoordinate(2.0, physicsGroup);
        flightTime_->setRange(0.05, 300.0);
        flightTime_->setSingleStep(0.5);
        speed_->setToolTip(bspvision::uiText(
            "4000 is the speed used by the BM-30 addon's HUD trajectory preview, not a verified sw_rocket_9m528_v3 launch speed.",
            "4000 — скорость в HUD-предсказании аддона БМ-30, а не подтверждённая начальная скорость sw_rocket_9m528_v3."));
        gravity_->setToolTip(bspvision::uiText(
            "Use the magnitude of physenv.GetGravity() for this server. Stock Garry's Mod sv_gravity is 600; servers can change it.",
            "Укажите модуль physenv.GetGravity() на сервере. Стандартное значение sv_gravity в Garry's Mod — 600, но сервер может его менять."));
        physicsForm->addRow(bspvision::uiText("Reference speed (units/s)", "Опорная скорость (ед./с)"), speed_);
        physicsForm->addRow(bspvision::uiText("Gravity (units/s²)", "Гравитация (ед./с²)"), gravity_);
        auto *bm30Preset = new QPushButton(
            bspvision::uiText("BM-30 · HUD estimate (4000 / 600)", "БМ-30 · оценка HUD (4000 / 600)"), physicsGroup);
        bm30Preset->setToolTip(bspvision::uiText(
            "Applies 4000 units/s from the addon's HUD predictor and stock GMod gravity 600. The rocket entity's real speed is still unknown.",
            "Подставляет 4000 ед./с из HUD-предсказателя аддона и стандартную гравитацию GMod 600. Реальная скорость сущности ракеты пока неизвестна."));
        physicsForm->addRow(bm30Preset);
        physicsForm->addRow(bspvision::uiText("Yaw / azimuth (°)", "Азимут (°)"), yaw_);
        physicsForm->addRow(bspvision::uiText("Pitch / elevation (°)", "Угол возвышения (°)"), pitch_);
        physicsForm->addRow(bspvision::uiText("Flight time (s)", "Время полёта (с)"), flightTime_);
        autoSolveTargetCheck_ = new QCheckBox(
            bspvision::uiText("Auto-solve target · shortest low arc", "Авторасчёт цели · кратчайшая низкая дуга"),
            physicsGroup);
        autoSolveTargetCheck_->setChecked(true);
        autoSolveTargetCheck_->setToolTip(bspvision::uiText(
            "Uses a straight XY azimuth between the two points, solves the low-time ballistic arc, then tries the high arc if the low arc intersects BSP surfaces. This is an ideal HUD-style estimate; it does not simulate missile guidance.",
            "Берёт прямой азимут между двумя точками, рассчитывает низкую баллистическую дугу, а при пересечении BSP пробует высокую. Это идеальная оценка для HUD, а не симуляция наведения управляемой ракеты."));
        physicsForm->addRow(autoSolveTargetCheck_);

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
            "BM-30 reference: 4000 units/s is copied from the addon's HUD preview only; it is not the confirmed missile speed. Gravity defaults to stock GMod 600. Guided-rocket behaviour, drag and live engine physics are not simulated.",
            "Ориентир для БМ-30: 4000 ед./с взято только из HUD-предсказания аддона — реальная скорость ракеты не подтверждена. Стандартная гравитация GMod — 600. Наведение ракеты, сопротивление и фактическая физика движка не моделируются."), tab);
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
            autoAimEnabled_ = false;
            autoAimStage_ = AutoAimStage::Disabled;
            autoAimStatus_ = bspvision::uiText("Auto aim stopped: target cleared.", "Автодоводка остановлена: цель удалена.");
            ballisticsView_->clearTrajectory();
            trajectoryCalculated_ = false;
            targetDefined_ = false;
            updateAutoSolveControls();
            ballisticsSummary_->setText(bspvision::uiText("Trajectory and overlay target cleared.", "Траектория и цель оверлея очищены."));
            syncOverlay();
        });

        auto invalidateSolution = [this] {
            if (autoSolveInProgress_) return;
            autoAimEnabled_ = false;
            autoAimStage_ = AutoAimStage::Disabled;
            autoAimStatus_ = bspvision::uiText("Auto aim stopped: ballistic solution changed.", "Автодоводка остановлена: баллистическое решение изменилось.");
            trajectoryCalculated_ = false;
            if (ballisticsView_) ballisticsView_->clearTrajectory();
            if (ballisticsSummary_) {
                ballisticsSummary_->setText(bspvision::uiText(
                    "Parameters changed. Enable Auto-solve or calculate the trajectory again.",
                    "Параметры изменены. Включите авторасчёт или пересчитайте траекторию."));
            }
            syncOverlay();
        };
        const QList<QDoubleSpinBox *> solveInputs = {
            originX_, originY_, originZ_, targetX_, targetY_, targetZ_, speed_, gravity_
        };
        for (QDoubleSpinBox *field : solveInputs) {
            connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [this, invalidateSolution](double) {
                if (autoSolveInProgress_) return;
                invalidateSolution();
                calculateArcAutomatically();
            });
        }
        const QList<QDoubleSpinBox *> manualArcInputs = {yaw_, pitch_, flightTime_};
        for (QDoubleSpinBox *field : manualArcInputs) {
            connect(field, qOverload<double>(&QDoubleSpinBox::valueChanged), this,
                    [invalidateSolution](double) { invalidateSolution(); });
        }
        connect(autoSolveTargetCheck_, &QCheckBox::toggled, this, [this](bool enabled) {
            updateAutoSolveControls();
            if (enabled) calculateArcAutomatically();
        });
        connect(bm30Preset, &QPushButton::clicked, this, [this] {
            speed_->setValue(4000.0);
            gravity_->setValue(600.0);
            if (autoSolveTargetCheck_ && autoSolveTargetCheck_->isChecked())
                calculateArcAutomatically();
        });
        updateAutoSolveControls();
    }

    void updateCropRegionLabel() {
        if (!cropRegionLabel_ || !screenCaptureX_ || !screenCaptureY_ ||
            !screenCaptureWidth_ || !screenCaptureHeight_) return;
        cropRegionLabel_->setText(bspvision::uiText(
            "Crop (physical screen pixels): X=%1, Y=%2, W=%3, H=%4",
            "Область (физические пиксели экрана): X=%1, Y=%2, Ш=%3, В=%4")
            .arg(screenCaptureX_->value()).arg(screenCaptureY_->value())
            .arg(screenCaptureWidth_->value()).arg(screenCaptureHeight_->value()));
    }

    void beginCropSelection() {
        QScreen *primary = QGuiApplication::primaryScreen();
        if (!primary) {
            if (screenReaderStatusLabel_) screenReaderStatusLabel_->setText(
                bspvision::uiText("No display is available for selection.", "Нет доступного экрана для выделения."));
            return;
        }
        autoAimEnabled_ = false;
        autoAimStage_ = AutoAimStage::Disabled;
        autoAimStatus_ = bspvision::uiText("Auto aim stopped: telemetry crop is being changed.", "Автодоводка остановлена: меняется область телеметрии.");
        if (screenCaptureTimer_) screenCaptureTimer_->stop();
        screenReader_.stop();

        auto *selector = new ScreenRegionSelector(primary, this);
        cropSelector_ = selector;
        selector->regionSelected = [this](const QRect &region) {
            cropSelector_ = nullptr;
            if (screenCaptureX_) screenCaptureX_->setValue(region.x());
            if (screenCaptureY_) screenCaptureY_->setValue(region.y());
            if (screenCaptureWidth_) screenCaptureWidth_->setValue(region.width());
            if (screenCaptureHeight_) screenCaptureHeight_->setValue(region.height());
            updateCropRegionLabel();
            statusBar()->showMessage(bspvision::uiText(
                "Crop selected. Starting cl_showpos template matching.",
                "Область выделена. Запускаю распознавание cl_showpos."));
            if (screenReaderEnabledCheck_ && !screenReaderEnabledCheck_->isChecked())
                screenReaderEnabledCheck_->setChecked(true);
            else
                startClShowPosReader();
        };
        selector->canceled = [this] {
            cropSelector_ = nullptr;
            if (screenReaderEnabledCheck_ && screenReaderEnabledCheck_->isChecked())
                startClShowPosReader();
        };
        selector->show();
        selector->raise();
        selector->activateWindow();
        selector->setFocus(Qt::OtherFocusReason);
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
        autoAimEnabled_ = false;
        autoAimStage_ = AutoAimStage::Disabled;
        autoAimStatus_ = bspvision::uiText("Auto aim stopped: telemetry reader off.", "Автодоводка остановлена: чтение телеметрии выключено.");
        if (screenCaptureTimer_) screenCaptureTimer_->stop();
        screenReader_.stop();
        if (screenReaderStatusLabel_) screenReaderStatusLabel_->setText(screenReader_.lastStatus());
        if (useScreenPositionButton_) useScreenPositionButton_->setEnabled(false);
        syncOverlay();
    }

    void pollClShowPosReader() {
        if (!screenReader_.isRunning()) return;
        bspvision::ClShowPosSample sample;
        QString status;
        if (screenReader_.poll(&sample, &status)) {
            if (currentYawInit_) currentYawInit_->setValue(sample.angles.y());
            if (currentPitchInit_) currentPitchInit_->setValue(sample.angles.x());
            if (useScreenPositionButton_) useScreenPositionButton_->setEnabled(true);
            processAutoAimSample(sample);
            if (liveTelemetryLabel_) {
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

    void updateAutoSolveControls() {
        const bool manualAnglesEnabled = !autoSolveTargetCheck_ ||
            !autoSolveTargetCheck_->isChecked() || !targetDefined_;
        if (yaw_) yaw_->setEnabled(manualAnglesEnabled);
        if (pitch_) pitch_->setEnabled(manualAnglesEnabled);
        if (flightTime_) flightTime_->setEnabled(manualAnglesEnabled);
    }

    void calculateArcAutomatically() {
        if (!autoSolveTargetCheck_ || !autoSolveTargetCheck_->isChecked() ||
            !targetDefined_ || autoSolveInProgress_) return;
        autoSolveInProgress_ = true;
        calculateArc(true, false);
        if (trajectoryCalculated_ && lastTrajectoryBlocked_) {
            // Prefer the minimum-time low arc, but fall back to the high arc
            // when BSP geometry blocks it and the high arc remains clear.
            calculateArc(true, true);
            if (trajectoryCalculated_ && !lastTrajectoryBlocked_ && ballisticsSummary_) {
                ballisticsSummary_->setText(ballisticsSummary_->text() +
                    bspvision::uiText(" Low arc was blocked; switched to the clear high arc.",
                                      " Низкая дуга перекрыта; выбрана свободная высокая дуга."));
            }
        }
        autoSolveInProgress_ = false;
    }

    void calculateArc(bool solveTarget, bool highArc = false) {
        const double ox = originX_->value(), oy = originY_->value(), oz = originZ_->value();
        const double tx = targetX_->value(), ty = targetY_->value(), tz = targetZ_->value();
        const double speed = speed_->value(), g = gravity_->value();
        if (autoAimEnabled_) {
            autoAimEnabled_ = false;
            autoAimStage_ = AutoAimStage::Disabled;
            autoAimStatus_ = bspvision::uiText("Auto aim stopped: recalculating trajectory.", "Автодоводка остановлена: пересчёт траектории.");
        }
        double yaw = qDegreesToRadians(yaw_->value());
        double pitch = qDegreesToRadians(pitch_->value());
        double duration = flightTime_->value();
        trajectoryCalculated_ = false;
        lastTrajectoryBlocked_ = false;
        if (ballisticsView_) ballisticsView_->clearTrajectory();
        syncOverlay();
        targetDefined_ = targetDefined_ || solveTarget;
        updateAutoSolveControls();

        if (solveTarget) {
            const double dx = tx - ox, dy = ty - oy, dz = tz - oz;
            const double range = std::hypot(dx, dy);
            if (range < 0.01) {
                if (autoSolveInProgress_) {
                    if (ballisticsSummary_) ballisticsSummary_->setText(
                        bspvision::uiText("Choose different launch and target points.", "Точки старта и цели должны различаться."));
                    syncOverlay();
                    return;
                }
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
                    if (autoSolveInProgress_) {
                        if (ballisticsSummary_) ballisticsSummary_->setText(
                            bspvision::uiText("No in-game ballistic solution for the selected speed, gravity and endpoints.",
                                              "Для выбранных игровых скорости, гравитации и точек решения нет."));
                        syncOverlay();
                        return;
                    }
                    QMessageBox::information(this, bspvision::uiText("No ballistic solution", "Нет баллистического решения"),
                        bspvision::uiText("The target is outside the ideal projectile envelope for this speed and gravity. Increase the game speed, reduce game gravity, or choose a nearer target.", "Цель недостижима в игровой модели при этих скорости и гравитации. Проверьте параметры или выберите точку ближе."));
                    return;
                }
                const double root = std::sqrt(discriminant);
                const double tangent = (v2 + (highArc ? root : -root)) / (g * range);
                pitch = std::atan(tangent);
            }
            const double horizontalSpeed = speed * std::cos(pitch);
            if (horizontalSpeed <= 0.01) {
                if (autoSolveInProgress_) {
                    if (ballisticsSummary_) ballisticsSummary_->setText(
                        bspvision::uiText("The selected target requires an unsuitable trajectory.", "Для выбранной цели получилась непригодная траектория."));
                    syncOverlay();
                    return;
                }
                QMessageBox::warning(this, bspvision::uiText("Ballistic solver", "Расчёт баллистики"),
                    bspvision::uiText("The computed elevation is too steep for a stable horizontal range.", "Полученный угол слишком велик для устойчивого расчёта дальности."));
                return;
            }
            duration = range / horizontalSpeed;
            yaw_->setValue(qRadiansToDegrees(yaw));
            pitch_->setValue(qRadiansToDegrees(pitch));
            flightTime_->setValue(std::clamp(duration, 0.05, 300.0));
            duration = flightTime_->value();
        }

        const double vx = speed * std::cos(pitch) * std::cos(yaw);
        const double vy = speed * std::cos(pitch) * std::sin(yaw);
        const double vz = speed * std::sin(pitch);
        QVector<QVector3D> points;
        const int steps = std::clamp(static_cast<int>(std::ceil(duration * 24.0)), 120, 2400);
        points.reserve(steps + 1);
        for (int i = 0; i <= steps; ++i) {
            const double t = duration * static_cast<double>(i) / steps;
            points.push_back(QVector3D(
                static_cast<float>(ox + vx * t),
                static_cast<float>(oy + vy * t),
                static_cast<float>(oz + vz * t - 0.5 * g * t * t)));
        }
        bool collisionTraceAvailable = false;
        bool collisionBlocked = false;
        double collisionFlightTime = 0.0;
        bspvision::SegmentHit collisionHit;

        if (bspFile_.isLoaded()) {
            if (!collisionBvhAttempted_) {
                collisionBvhAttempted_ = true;
                statusBar()->showMessage(bspvision::uiText(
                    "Building BSP surface BVH for trajectory checks…",
                    "Строю BVH поверхностей BSP для проверки траектории…"));
                QApplication::processEvents();
                if (!collisionBvh_.build(bspFile_.facePolygons(false), &collisionBvhError_))
                    collisionBvhError_ = collisionBvhError_.isEmpty()
                        ? QStringLiteral("The surface BVH could not be built.") : collisionBvhError_;
                else
                    collisionBvhReady_ = true;
            }

            if (collisionBvhReady_) {
                collisionTraceAvailable = true;
                for (int i = 1; i < points.size(); ++i) {
                    bspvision::SegmentHit hit;
                    if (!collisionBvh_.intersectSegment(points[i - 1], points[i], &hit)) continue;
                    collisionBlocked = true;
                    collisionHit = hit;
                    collisionFlightTime = duration *
                        (static_cast<double>(i - 1) + hit.fraction) / static_cast<double>(steps);
                    points[i] = hit.position;
                    points.resize(i + 1);
                    break;
                }
            }
        }

        lastTrajectoryBlocked_ = collisionBlocked;
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
        if (collisionBlocked) {
            summary += bspvision::uiText(
                " BLOCKED by BSP surface near (%1, %2, %3) at t=%4 s, material=%5. This is a render-surface approximation, not Source engine collision.",
                " ПРЕПЯТСТВИЕ: поверхность BSP около (%1, %2, %3), t=%4 с, материал=%5. Это приближение по треугольникам рендера, а не физическая коллизия Source.")
                .arg(collisionHit.position.x(), 0, 'f', 1)
                .arg(collisionHit.position.y(), 0, 'f', 1)
                .arg(collisionHit.position.z(), 0, 'f', 1)
                .arg(collisionFlightTime, 0, 'f', 2)
                .arg(collisionHit.materialName.isEmpty() ? QStringLiteral("unknown") : collisionHit.materialName);
        } else if (collisionTraceAvailable) {
            summary += bspvision::uiText(
                " BSP triangle-surface trace: clear (props/engine collision are not included).",
                " Проверка треугольников BSP: препятствий не найдено (пропы и физика движка не учитываются).");
            if (targetDefined_)
                summary += bspvision::uiText(" Ideal endpoint error: %1 units.", " Ошибка в конечной точке: %1 ед.").arg(miss, 0, 'f', 1);
        } else if (bspFile_.isLoaded()) {
            summary += bspvision::uiText(
                " BSP surface trace unavailable: %1", " Проверка поверхности BSP недоступна: %1").arg(collisionBvhError_);
        } else {
            summary += bspvision::uiText(
                " No BSP map loaded; occlusion was not checked.", " Карта BSP не загружена; препятствия не проверялись.");
            if (targetDefined_)
                summary += bspvision::uiText(" Ideal endpoint error: %1 units.", " Ошибка в конечной точке: %1 ед.").arg(miss, 0, 'f', 1);
        }
        summary += bspvision::uiText(" Green = launch, red = target, amber = trajectory. Live angles come from the cl_showpos screen reader.",
                                     " Зелёный = запуск, красный = цель, янтарный = траектория. Текущие углы берутся из экранного чтения cl_showpos.");
        ballisticsSummary_->setText(summary);
        syncOverlay();
    }

    enum class AutoAimStage { Disabled, ProbeYaw, WaitYaw, ProbePitch, WaitPitch, Active };

    static double angleDifference(double target, double current) {
        double delta = std::fmod(target - current + 540.0, 360.0);
        if (delta < 0.0) delta += 360.0;
        return delta - 180.0;
    }

    bool isGarrysModForeground() const {
#if defined(Q_OS_WIN)
        const HWND foreground = GetForegroundWindow();
        if (!foreground) return false;
        DWORD processId = 0;
        GetWindowThreadProcessId(foreground, &processId);
        if (!processId) return false;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, processId);
        if (!process) return false;
        wchar_t imagePath[32768] = {};
        DWORD pathLength = static_cast<DWORD>(sizeof(imagePath) / sizeof(imagePath[0]));
        const BOOL ok = QueryFullProcessImageNameW(process, 0, imagePath, &pathLength);
        CloseHandle(process);
        if (!ok) return false;
        const QString executable =
            QFileInfo(QString::fromWCharArray(imagePath, static_cast<qsizetype>(pathLength))).fileName();
        // Current Windows installs can run either the classic client or the
        // newer 64-bit executable introduced for the Windows client.
        return executable.compare(QStringLiteral("gmod.exe"), Qt::CaseInsensitive) == 0 ||
               executable.compare(QStringLiteral("gmod_win64.exe"), Qt::CaseInsensitive) == 0;
#else
        return false;
#endif
    }

    bool sendRelativeMouse(int dx, int dy) {
#if defined(Q_OS_WIN)
        if (dx == 0 && dy == 0) return true;
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dx = dx;
        input.mi.dy = dy;
        input.mi.dwFlags = MOUSEEVENTF_MOVE;
        return SendInput(1, &input, sizeof(INPUT)) == 1;
#else
        Q_UNUSED(dx);
        Q_UNUSED(dy);
        return false;
#endif
    }

    void stopAutoAim(const QString &reason) {
        autoAimEnabled_ = false;
        autoAimStage_ = AutoAimStage::Disabled;
        autoAimStatus_ = reason;
        syncOverlay();
    }

    void toggleAutoAim() {
        if (autoAimEnabled_) {
            stopAutoAim(bspvision::uiText("AUTO AIM OFF · manual control restored.",
                                          "АВТОСУО ВЫКЛ · ручное управление восстановлено."));
            statusBar()->showMessage(autoAimStatus_);
            return;
        }
        if (!targetDefined_ || !trajectoryCalculated_) {
            statusBar()->showMessage(bspvision::uiText(
                "Calculate a ballistic solution first, then press F7 in Garry's Mod.",
                "Сначала рассчитайте траекторию, затем нажмите F7 в Garry's Mod."));
            return;
        }
        if (lastTrajectoryBlocked_) {
            statusBar()->showMessage(bspvision::uiText(
                "Auto aim blocked: the selected ballistic path intersects BSP geometry. Choose a clear arc first.",
                "Автодоводка недоступна: рассчитанная траектория пересекает геометрию BSP. Сначала выберите свободную дугу."));
            return;
        }
#if defined(Q_OS_WIN)
        if (!isGarrysModForeground()) {
            statusBar()->showMessage(bspvision::uiText(
                "Focus the Garry's Mod game window before enabling auto aim.",
                "Перед включением автодоводки переключитесь на окно Garry's Mod."));
            return;
        }
        if (!screenReader_.isRunning()) startClShowPosReader();
        if (!screenReader_.isRunning()) {
            statusBar()->showMessage(bspvision::uiText(
                "cl_showpos reader is not running. Enable it and select the pos:/ang: crop first.",
                "Не запущено чтение cl_showpos. Сначала включите его и выделите строки pos:/ang:."));
            return;
        }
        autoAimEnabled_ = true;
        autoAimStage_ = AutoAimStage::ProbeYaw;
        autoAimProbeAttempts_ = 0;
        autoAimProbeWaitTicks_ = 0;
        mouseYawDegreesPerCount_ = 0.0;
        mousePitchDegreesPerCount_ = 0.0;
        autoAimStatus_ = bspvision::uiText(
            "AUTO AIM · calibrating mouse response (keep Garry's Mod focused)",
            "АВТОСУО · калибровка мыши (не переключайтесь из Garry's Mod)");
        if (overlay_ && !overlay_->isVisible()) startExternalOverlay();
        syncOverlay();
        statusBar()->showMessage(bspvision::uiText(
            "Auto fire control enabled. F7 again immediately restores manual control.",
            "Автоматическая СУО включена. Повторное F7 немедленно вернёт ручное управление."));
#else
        statusBar()->showMessage(bspvision::uiText(
            "Automatic mouse control is currently supported only on Windows.",
            "Автоматическое управление мышью сейчас поддерживается только в Windows."));
#endif
    }

    void processAutoAimSample(const bspvision::ClShowPosSample &sample) {
        if (!autoAimEnabled_ || !yaw_ || !pitch_) return;
#if defined(Q_OS_WIN)
        if (!isGarrysModForeground()) {
            autoAimStatus_ = bspvision::uiText(
                "AUTO AIM PAUSED · Garry's Mod is not foreground",
                "АВТОСУО ПАУЗА · окно Garry's Mod неактивно");
            return; // Never inject mouse input into another application.
        }
        if (sample.confidence < 0.72) {
            autoAimStatus_ = bspvision::uiText(
                "AUTO AIM HOLD · cl_showpos confidence is too low",
                "АВТОСУО ОЖИДАНИЕ · низкая точность чтения cl_showpos");
            return;
        }

        constexpr int probeCounts = 10;
        if (autoAimStage_ == AutoAimStage::ProbeYaw) {
            autoAimProbeYaw_ = sample.angles.y();
            if (!sendRelativeMouse(probeCounts, 0)) {
                stopAutoAim(bspvision::uiText("AUTO AIM ERROR · mouse input was rejected.",
                                              "ОШИБКА АВТОСУО · система отклонила ввод мыши."));
                return;
            }
            autoAimProbeWaitTicks_ = 0;
            autoAimStage_ = AutoAimStage::WaitYaw;
            autoAimStatus_ = bspvision::uiText("AUTO AIM · measuring horizontal response",
                                               "АВТОСУО · измерение горизонтального отклика");
            return;
        }
        if (autoAimStage_ == AutoAimStage::WaitYaw) {
            ++autoAimProbeWaitTicks_;
            const double response = angleDifference(sample.angles.y(), autoAimProbeYaw_);
            if (std::abs(response) >= 0.004 && std::abs(response) < 20.0) {
                mouseYawDegreesPerCount_ = response / static_cast<double>(probeCounts);
                autoAimProbeAttempts_ = 0;
                autoAimStage_ = AutoAimStage::ProbePitch;
            } else if (autoAimProbeWaitTicks_ >= 3) {
                if (autoAimProbeAttempts_ < 2) {
                    ++autoAimProbeAttempts_;
                    autoAimProbeYaw_ = sample.angles.y();
                    if (!sendRelativeMouse(probeCounts, 0)) {
                        stopAutoAim(bspvision::uiText("AUTO AIM ERROR · cannot calibrate mouse.",
                                                      "ОШИБКА АВТОСУО · не удалось откалибровать мышь."));
                        return;
                    }
                    autoAimProbeWaitTicks_ = 0;
                } else {
                    stopAutoAim(bspvision::uiText(
                        "AUTO AIM FAILED · yaw did not respond. If GMod uses raw input, try console command m_rawinput 0 and press F7 again.",
                        "АВТОСУО НЕ ЗАПУЩЕНА · yaw не реагирует. Если включён raw input, попробуйте в консоли m_rawinput 0 и снова нажмите F7."));
                    return;
                }
            }
            return;
        }
        if (autoAimStage_ == AutoAimStage::ProbePitch) {
            autoAimProbePitch_ = sample.angles.x();
            if (!sendRelativeMouse(0, probeCounts)) {
                stopAutoAim(bspvision::uiText("AUTO AIM ERROR · mouse input was rejected.",
                                              "ОШИБКА АВТОСУО · система отклонила ввод мыши."));
                return;
            }
            autoAimProbeWaitTicks_ = 0;
            autoAimStage_ = AutoAimStage::WaitPitch;
            autoAimStatus_ = bspvision::uiText("AUTO AIM · measuring vertical response",
                                               "АВТОСУО · измерение вертикального отклика");
            return;
        }
        if (autoAimStage_ == AutoAimStage::WaitPitch) {
            ++autoAimProbeWaitTicks_;
            const double response = sample.angles.x() - autoAimProbePitch_;
            if (std::abs(response) >= 0.004 && std::abs(response) < 20.0) {
                mousePitchDegreesPerCount_ = response / static_cast<double>(probeCounts);
                autoAimStage_ = AutoAimStage::Active;
            } else if (autoAimProbeWaitTicks_ >= 3) {
                if (autoAimProbeAttempts_ < 2) {
                    ++autoAimProbeAttempts_;
                    autoAimProbePitch_ = sample.angles.x();
                    if (!sendRelativeMouse(0, probeCounts)) {
                        stopAutoAim(bspvision::uiText("AUTO AIM ERROR · cannot calibrate mouse.",
                                                      "ОШИБКА АВТОСУО · не удалось откалибровать мышь."));
                        return;
                    }
                    autoAimProbeWaitTicks_ = 0;
                } else {
                    stopAutoAim(bspvision::uiText(
                        "AUTO AIM FAILED · pitch did not respond. If GMod uses raw input, try console command m_rawinput 0 and press F7 again.",
                        "АВТОСУО НЕ ЗАПУЩЕНА · pitch не реагирует. Если включён raw input, попробуйте в консоли m_rawinput 0 и снова нажмите F7."));
                    return;
                }
            }
            return;
        }
        if (autoAimStage_ == AutoAimStage::Active) {
            if (std::abs(mouseYawDegreesPerCount_) < 0.0002 ||
                std::abs(mousePitchDegreesPerCount_) < 0.0002) {
                stopAutoAim(bspvision::uiText("AUTO AIM STOPPED · invalid mouse calibration.",
                                              "АВТОСУО ОСТАНОВЛЕНА · некорректная калибровка мыши."));
                return;
            }
            const double yawError = angleDifference(yaw_->value(), sample.angles.y());
            const double pitchError = -pitch_->value() - sample.angles.x();
            constexpr double deadbandDegrees = 0.08;
            constexpr double proportionalGain = 0.62;
            constexpr int maxCountsPerSample = 32;
            const int mouseX = std::abs(yawError) <= deadbandDegrees ? 0 :
                std::clamp(static_cast<int>(std::lround(
                    (yawError / mouseYawDegreesPerCount_) * proportionalGain)),
                    -maxCountsPerSample, maxCountsPerSample);
            const int mouseY = std::abs(pitchError) <= deadbandDegrees ? 0 :
                std::clamp(static_cast<int>(std::lround(
                    (pitchError / mousePitchDegreesPerCount_) * proportionalGain)),
                    -maxCountsPerSample, maxCountsPerSample);
            if ((mouseX != 0 || mouseY != 0) && !sendRelativeMouse(mouseX, mouseY)) {
                stopAutoAim(bspvision::uiText("AUTO AIM ERROR · mouse input failed.",
                                              "ОШИБКА АВТОСУО · не удалось отправить движение мыши."));
                return;
            }
            const bool onTarget = std::abs(yawError) <= 0.25 && std::abs(pitchError) <= 0.25;
            autoAimStatus_ = bspvision::uiText(
                "AUTO AIM %1 · yaw error %2° · pitch error %3°",
                "АВТОСУО %1 · ошибка yaw %2° · ошибка pitch %3°")
                .arg(onTarget ? QStringLiteral("LOCK") : QStringLiteral("CORRECTING"))
                .arg(yawError, 0, 'f', 2).arg(pitchError, 0, 'f', 2);
        }
#else
        Q_UNUSED(sample);
#endif
    }

    void startExternalOverlay() {
        QScreen *targetScreen = screen();
        if (!targetScreen) targetScreen = QGuiApplication::primaryScreen();
        if (targetScreen) overlay_->showOverlayOn(targetScreen->geometry());
        syncOverlay();
    }

    void stopExternalOverlay() {
        autoAimEnabled_ = false;
        autoAimStage_ = AutoAimStage::Disabled;
        autoAimStatus_ = bspvision::uiText("Auto aim stopped.", "Автодоводка остановлена.");
        if (overlay_) overlay_->hide();
        syncOverlay();
    }

    void syncOverlay() {
        if (!overlay_) return;
        const double cy = currentYawInit_ ? currentYawInit_->value() : 0.0;
        const double cp = currentPitchInit_ ? currentPitchInit_->value() : 0.0;
        overlay_->setCurrentAngles(cy, cp);
        overlay_->setDesiredAngles(yaw_ ? yaw_->value() : 0.0, pitch_ ? -pitch_->value() : 0.0);
        overlay_->setHorizontalFov(overlayFov_ ? overlayFov_->value() : 90.0);
        const double range = originX_ && targetX_
            ? std::hypot(targetX_->value() - originX_->value(), targetY_->value() - originY_->value()) : 0.0;
        overlay_->setTargetDetails(range, flightTime_ ? flightTime_->value() : 0.0, loadedMapName_);
        const bool enabled = autoAimEnabled_ && targetDefined_ && trajectoryCalculated_;
        overlay_->setTargetEnabled(enabled);

        QString state;
        if (screenReaderEnabledCheck_ && screenReaderEnabledCheck_->isChecked()) {
            bspvision::ClShowPosSample sample;
            state = screenReader_.latestSample(&sample)
                ? QStringLiteral("cl_showpos active · %1% match").arg(qRound(sample.confidence * 100.0))
                : screenReader_.lastStatus();
        } else {
            state = QStringLiteral("cl_showpos reader stopped");
        }
        if (autoAimEnabled_) state = autoAimStatus_;
        if (liveStatusLabel_) liveStatusLabel_->setText(state);
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

    static constexpr int AutoAimHotkeyId = 0xB570;
    AutoAimStage autoAimStage_ = AutoAimStage::Disabled;
    bool autoAimEnabled_ = false;
    int autoAimProbeAttempts_ = 0;
    int autoAimProbeWaitTicks_ = 0;
    double autoAimProbeYaw_ = 0.0;
    double autoAimProbePitch_ = 0.0;
    double mouseYawDegreesPerCount_ = 0.0;
    double mousePitchDegreesPerCount_ = 0.0;
    QString autoAimStatus_ = QStringLiteral("Auto aim OFF");
    QTabWidget *tabs_ = nullptr;
    QMenu *settingsMenu_ = nullptr;
    QMenu *languageMenu_ = nullptr;
    QAction *englishAction_ = nullptr;
    QAction *russianAction_ = nullptr;
    QCheckBox *showSkyboxCheck_ = nullptr;
    QCheckBox *heightfieldCheck_ = nullptr;
    QComboBox *contourIntervalCombo_ = nullptr;
    bool showSkybox_ = false;
    bool heightfieldVisible_ = false;
    int heightfieldContourInterval_ = 128;
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
    QCheckBox *autoSolveTargetCheck_ = nullptr;
    bool autoSolveInProgress_ = false;
    bool lastTrajectoryBlocked_ = false;
    QLabel *liveStatusLabel_ = nullptr, *liveTelemetryLabel_ = nullptr;
    QLabel *screenReaderStatusLabel_ = nullptr;
    QLabel *cropRegionLabel_ = nullptr;
    QCheckBox *screenReaderEnabledCheck_ = nullptr;
    QSpinBox *screenCaptureX_ = nullptr, *screenCaptureY_ = nullptr;
    QSpinBox *screenCaptureWidth_ = nullptr, *screenCaptureHeight_ = nullptr;
    QPushButton *useScreenPositionButton_ = nullptr;
    QDoubleSpinBox *currentYawInit_ = nullptr, *currentPitchInit_ = nullptr;
    QDoubleSpinBox *overlayFov_ = nullptr;
    QTimer *overlayTimer_ = nullptr;
    QTimer *screenCaptureTimer_ = nullptr;
    ScreenRegionSelector *cropSelector_ = nullptr;
    bspvision::ClShowPosReader screenReader_;
    bspvision::Heightfield heightfield_;
    bspvision::TriangleBvh collisionBvh_;
    bool collisionBvhReady_ = false;
    bool collisionBvhAttempted_ = false;
    QString collisionBvhError_;
    bspvision::LiveOverlay *overlay_ = nullptr;
    QString loadedMapName_;
    int pickMode_ = 0;
    bool targetDefined_ = false;
#if defined(Q_OS_WIN)
    bool hotkeyRegistered_ = false;
#endif
    bspvision::BspFile bspFile_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
