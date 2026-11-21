#include "bsp_file.hpp"
#include "bsp_map_view.hpp"
#include "bsp_3d_view.hpp"
#include "ballistics_view.hpp"

#include <QAbstractItemView>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QGuiApplication>
#include <QHeaderView>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QPixmap>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollArea>
#include <QStatusBar>
#include <QTimer>
#include <QTabWidget>
#include <QTableWidget>
#include <QTableWidgetItem>
#include <QStringList>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <opencv2/calib3d.hpp>
#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <QVector3D>
#include <QtMath>
#include <vector>

namespace {
QPixmap toPixmap(const cv::Mat &image) {
    if (image.empty()) return {};
    cv::Mat rgb;
    if (image.channels() == 1) cv::cvtColor(image, rgb, cv::COLOR_GRAY2RGB);
    else cv::cvtColor(image, rgb, cv::COLOR_BGR2RGB);
    const QImage view(rgb.data, rgb.cols, rgb.rows, static_cast<qsizetype>(rgb.step),
                      QImage::Format_RGB888);
    return QPixmap::fromImage(view.copy());
}

QLabel *makeImageView(const QString &text, QWidget *parent) {
    auto *view = new QLabel(text, parent);
    view->setAlignment(Qt::AlignCenter);
    view->setMinimumSize(320, 220);
    view->setStyleSheet(QStringLiteral(
        "QLabel { background: #20242b; color: #d5d9e0; border: 1px solid #454b55; }"));
    return view;
}

QScrollArea *wrap(QWidget *child, QWidget *parent) {
    auto *scroll = new QScrollArea(parent);
    scroll->setWidget(child);
    scroll->setWidgetResizable(true);
    return scroll;
}
}

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("BSP Vision — geometry and visual matching"));
        resize(1360, 900);

        auto *tabs = new QTabWidget(this);
        setCentralWidget(tabs);
        buildImageTab(tabs);
        buildBspTab(tabs);
        buildBallisticsTab(tabs);
        statusBar()->showMessage(QStringLiteral(
            "Offline diagnostic prototype. Load a BSP or two images to begin."));
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        refreshImages();
    }

private:
    void buildImageTab(QTabWidget *tabs) {
        auto *tab = new QWidget(tabs);
        auto *layout = new QVBoxLayout(tab);
        auto *controls = new QHBoxLayout();
        auto *loadReference = new QPushButton(QStringLiteral("Load reference image…"), tab);
        auto *loadCurrent = new QPushButton(QStringLiteral("Load screenshot…"), tab);
        auto *captureScreen = new QPushButton(QStringLiteral("Capture primary screen (2 s)"), tab);
        auto *match = new QPushButton(QStringLiteral("Find visual matches"), tab);
        match->setEnabled(false);
        controls->addWidget(loadReference);
        controls->addWidget(loadCurrent);
        controls->addWidget(captureScreen);
        controls->addWidget(match);
        controls->addStretch();
        layout->addLayout(controls);

        auto *hint = new QLabel(QStringLiteral(
            "Local image analysis: ORB feature matching plus RANSAC geometric filtering. "
            "Screen capture hides this window for two seconds. No game-process access or input automation."), tab);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *images = new QHBoxLayout();
        referenceView_ = makeImageView(QStringLiteral("Reference image not loaded"), tab);
        currentView_ = makeImageView(QStringLiteral("Screenshot not loaded"), tab);
        images->addWidget(wrap(referenceView_, tab), 1);
        images->addWidget(wrap(currentView_, tab), 1);
        layout->addLayout(images, 2);

        resultView_ = makeImageView(QStringLiteral("Match visualization will appear here."), tab);
        layout->addWidget(wrap(resultView_, tab), 2);
        matchSummary_ = new QLabel(QStringLiteral(
            "Load two images to compare local visual features."), tab);
        matchSummary_->setWordWrap(true);
        layout->addWidget(matchSummary_);
        tabs->addTab(tab, QStringLiteral("Visual matching"));

        connect(loadReference, &QPushButton::clicked, this, [this, match] {
            const QString path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Choose reference image"), {},
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty()) return;
            reference_ = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
            if (reference_.empty()) {
                QMessageBox::warning(this, QStringLiteral("Image error"),
                    QStringLiteral("Could not read image: ") + path);
                return;
            }
            referenceView_->setToolTip(QFileInfo(path).fileName());
            refreshImages();
            updateMatchEnabled(match);
            statusBar()->showMessage(QStringLiteral("Reference loaded: %1 × %2")
                .arg(reference_.cols).arg(reference_.rows));
        });

        connect(loadCurrent, &QPushButton::clicked, this, [this, match] {
            const QString path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Choose screenshot"), {},
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty()) return;
            current_ = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
            if (current_.empty()) {
                QMessageBox::warning(this, QStringLiteral("Image error"),
                    QStringLiteral("Could not read image: ") + path);
                return;
            }
            currentView_->setToolTip(QFileInfo(path).fileName());
            refreshImages();
            updateMatchEnabled(match);
            statusBar()->showMessage(QStringLiteral("Screenshot loaded: %1 × %2")
                .arg(current_.cols).arg(current_.rows));
        });
        connect(captureScreen, &QPushButton::clicked, this, [this, match] {
            QScreen *screen = QGuiApplication::primaryScreen();
            if (!screen) {
                QMessageBox::warning(this, QStringLiteral("Screen capture"),
                    QStringLiteral("No primary screen is available."));
                return;
            }

            statusBar()->showMessage(QStringLiteral(
                "Capturing primary screen in 2 seconds — switch to the game now."));
            hide();
            QTimer::singleShot(2000, this, [this, screen, match] {
                const QPixmap capture = screen->grabWindow(0);
                showNormal();
                raise();
                activateWindow();
                if (capture.isNull()) {
                    QMessageBox::warning(this, QStringLiteral("Screen capture"),
                        QStringLiteral("Screen capture failed. Try borderless/windowed mode or load a screenshot file."));
                    return;
                }

                QImage rgb = capture.toImage().convertToFormat(QImage::Format_RGB888);
                cv::Mat rgbView(rgb.height(), rgb.width(), CV_8UC3,
                                rgb.bits(), static_cast<size_t>(rgb.bytesPerLine()));
                cv::cvtColor(rgbView, current_, cv::COLOR_RGB2BGR);
                currentView_->setToolTip(QStringLiteral("Captured from the primary monitor"));
                refreshImages();
                updateMatchEnabled(match);
                statusBar()->showMessage(QStringLiteral("Captured screen: %1 × %2")
                    .arg(current_.cols).arg(current_.rows));
            });
        });
        connect(match, &QPushButton::clicked, this, [this] { findMatches(); });
    }

    void buildBspTab(QTabWidget *tabs) {
        auto *tab = new QWidget(tabs);
        auto *layout = new QVBoxLayout(tab);
        auto *controls = new QHBoxLayout();
        auto *loadBsp = new QPushButton(QStringLiteral("Load Source BSP v20…"), tab);
        controls->addWidget(loadBsp);
        controls->addStretch();
        layout->addLayout(controls);

        bspSummary_ = new QLabel(QStringLiteral(
            "No map loaded. This parser currently accepts Source BSP version 20."), tab);
        bspSummary_->setTextInteractionFlags(Qt::TextSelectableByMouse);
        bspSummary_->setWordWrap(true);
        layout->addWidget(bspSummary_);

        auto *content = new QHBoxLayout();
        auto *previewTabs = new QTabWidget(tab);
        mapView_ = new bspvision::BspMapView(previewTabs);
        meshView_ = new bspvision::Bsp3DView(previewTabs);
        previewTabs->addTab(mapView_, QStringLiteral("Top-down XY"));
        previewTabs->addTab(meshView_, QStringLiteral("Orbit 3D"));
        content->addWidget(previewTabs, 3);

        lumpTable_ = new QTableWidget(tab);
        lumpTable_->setColumnCount(4);
        lumpTable_->setHorizontalHeaderLabels({
            QStringLiteral("Lump"), QStringLiteral("Bytes"),
            QStringLiteral("Records"), QStringLiteral("Ver.")});
        lumpTable_->horizontalHeader()->setSectionResizeMode(0, QHeaderView::Stretch);
        lumpTable_->horizontalHeader()->setSectionResizeMode(1, QHeaderView::ResizeToContents);
        lumpTable_->horizontalHeader()->setSectionResizeMode(2, QHeaderView::ResizeToContents);
        lumpTable_->horizontalHeader()->setSectionResizeMode(3, QHeaderView::ResizeToContents);
        lumpTable_->setEditTriggers(QAbstractItemView::NoEditTriggers);
        lumpTable_->setSelectionBehavior(QAbstractItemView::SelectRows);
        lumpTable_->setAlternatingRowColors(true);
        content->addWidget(lumpTable_, 2);
        layout->addLayout(content, 1);

        coordinateLabel_ = new QLabel(QStringLiteral(
            "Click the map preview to inspect an XY coordinate. Z is not unique in a top-down view."),
            tab);
        coordinateLabel_->setWordWrap(true);
        layout->addWidget(coordinateLabel_);
        tabs->addTab(tab, QStringLiteral("BSP geometry"));

        connect(loadBsp, &QPushButton::clicked, this, [this] {
            const QString path = QFileDialog::getOpenFileName(this,
                QStringLiteral("Open Source BSP v20 map"), {},
                QStringLiteral("BSP maps (*.bsp);;All files (*.*)"));
            if (path.isEmpty()) return;

            QString error;
            if (!bspFile_.load(path, &error)) {
                QMessageBox::warning(this, QStringLiteral("BSP load failed"), error);
                return;
            }

            const QVector<bspvision::BspPolygon> polygons = bspFile_.facePolygons();
            mapView_->setMap(bspFile_.vertices(), polygons);
            meshView_->setMap(bspFile_.vertices(), polygons);
            ballisticsView_->setMap(bspFile_.vertices(), polygons);
            bspSummary_->setText(bspFile_.summary());
            populateLumpTable();
            coordinateLabel_->setText(QStringLiteral(
                "Click inside the preview to inspect XY coordinates. Z is ambiguous in this top-down "
                "view because maps can contain multiple floors."));
            statusBar()->showMessage(QStringLiteral("Loaded BSP: %1").arg(QFileInfo(path).fileName()));
        });

        mapView_->pointSelected = [this](double x, double y) {
            coordinateLabel_->setText(QStringLiteral(
                "Selected top-down coordinate: X = %1, Y = %2. Set Z manually for the correct floor.")
                .arg(x, 0, 'f', 2).arg(y, 0, 'f', 2));
            if (pickMode_ == 1) {
                originX_->setValue(x); originY_->setValue(y);
                pickMode_ = 0;
                statusBar()->showMessage(QStringLiteral("Launch point set from map. Set launch Z manually."));
            } else if (pickMode_ == 2) {
                targetX_->setValue(x); targetY_->setValue(y);
                targetDefined_ = true;
                pickMode_ = 0;
                statusBar()->showMessage(QStringLiteral("Target point set from map. Set target Z manually if needed."));
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

        auto *launchGroup = new QGroupBox(QStringLiteral("Launch point · Source world units"), tab);
        auto *launchForm = new QFormLayout(launchGroup);
        originX_ = makeCoordinate(0.0, launchGroup);
        originY_ = makeCoordinate(0.0, launchGroup);
        originZ_ = makeCoordinate(32.0, launchGroup);
        launchForm->addRow(QStringLiteral("X"), originX_);
        launchForm->addRow(QStringLiteral("Y"), originY_);
        launchForm->addRow(QStringLiteral("Z / height"), originZ_);
        auto *pickOrigin = new QPushButton(QStringLiteral("Pick XY on map…"), launchGroup);
        launchForm->addRow(pickOrigin);

        auto *targetGroup = new QGroupBox(QStringLiteral("Target point"), tab);
        auto *targetForm = new QFormLayout(targetGroup);
        targetX_ = makeCoordinate(512.0, targetGroup);
        targetY_ = makeCoordinate(0.0, targetGroup);
        targetZ_ = makeCoordinate(32.0, targetGroup);
        targetForm->addRow(QStringLiteral("X"), targetX_);
        targetForm->addRow(QStringLiteral("Y"), targetY_);
        targetForm->addRow(QStringLiteral("Z / height"), targetZ_);
        auto *pickTarget = new QPushButton(QStringLiteral("Pick XY on map…"), targetGroup);
        targetForm->addRow(pickTarget);

        auto *physicsGroup = new QGroupBox(QStringLiteral("Projectile model"), tab);
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
        physicsForm->addRow(QStringLiteral("Speed (units/s)"), speed_);
        physicsForm->addRow(QStringLiteral("Gravity (units/s²)"), gravity_);
        physicsForm->addRow(QStringLiteral("Yaw (°)"), yaw_);
        physicsForm->addRow(QStringLiteral("Pitch / elevation (°)"), pitch_);
        physicsForm->addRow(QStringLiteral("Flight time (s)"), flightTime_);

        controls->addWidget(launchGroup, 1);
        controls->addWidget(targetGroup, 1);
        controls->addWidget(physicsGroup, 1);
        layout->addLayout(controls);

        auto *actions = new QHBoxLayout();
        auto *calculate = new QPushButton(QStringLiteral("Calculate current arc"), tab);
        auto *solveLow = new QPushButton(QStringLiteral("Solve target · low arc"), tab);
        auto *solveHigh = new QPushButton(QStringLiteral("Solve target · high arc"), tab);
        auto *clear = new QPushButton(QStringLiteral("Clear path"), tab);
        actions->addWidget(calculate);
        actions->addWidget(solveLow);
        actions->addWidget(solveHigh);
        actions->addWidget(clear);
        actions->addStretch();
        layout->addLayout(actions);

        ballisticsView_ = new bspvision::BallisticsView(tab);
        layout->addWidget(ballisticsView_, 1);
        ballisticsSummary_ = new QLabel(QStringLiteral(
            "Ideal projectile model only: tune speed and gravity to the game. Obstacles, drag, splash radius and engine-specific projectile behavior are not simulated."), tab);
        ballisticsSummary_->setWordWrap(true);
        layout->addWidget(ballisticsSummary_);
        tabs->addTab(tab, QStringLiteral("Ballistic lab"));

        connect(pickOrigin, &QPushButton::clicked, this, [this, tabs] {
            pickMode_ = 1;
            tabs->setCurrentIndex(1);
            statusBar()->showMessage(QStringLiteral("Click the top-down XY map to set launch X/Y; set Z in Ballistic lab."));
        });
        connect(pickTarget, &QPushButton::clicked, this, [this, tabs] {
            pickMode_ = 2;
            tabs->setCurrentIndex(1);
            statusBar()->showMessage(QStringLiteral("Click the top-down XY map to set target X/Y; set Z in Ballistic lab."));
        });
        connect(calculate, &QPushButton::clicked, this, [this] { calculateArc(false); });
        connect(solveLow, &QPushButton::clicked, this, [this] { calculateArc(true, false); });
        connect(solveHigh, &QPushButton::clicked, this, [this] { calculateArc(true, true); });
        connect(clear, &QPushButton::clicked, this, [this] {
            ballisticsView_->clearTrajectory();
            ballisticsSummary_->setText(QStringLiteral("Trajectory cleared."));
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

        if (solveTarget) {
            const double dx = tx - ox, dy = ty - oy, dz = tz - oz;
            const double range = std::hypot(dx, dy);
            if (range < 0.01) {
                QMessageBox::warning(this, QStringLiteral("Ballistic solver"),
                    QStringLiteral("Launch point and target must differ in XY."));
                return;
            }
            yaw = std::atan2(dy, dx);
            if (g <= 0.0) {
                pitch = std::atan2(dz, range);
            } else {
                const double v2 = speed * speed;
                const double discriminant = v2 * v2 - g * (g * range * range + 2.0 * dz * v2);
                if (discriminant < 0.0) {
                    QMessageBox::information(this, QStringLiteral("No ballistic solution"),
                        QStringLiteral("The target is outside the ideal projectile envelope for this speed and gravity. Increase speed, reduce gravity, or choose a nearer target."));
                    return;
                }
                const double root = std::sqrt(discriminant);
                const double tangent = (v2 + (highArc ? root : -root)) / (g * range);
                pitch = std::atan(tangent);
            }
            const double horizontalSpeed = speed * std::cos(pitch);
            if (horizontalSpeed <= 0.01) {
                QMessageBox::warning(this, QStringLiteral("Ballistic solver"),
                    QStringLiteral("The computed elevation is too steep for a stable horizontal range."));
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
        const QVector3D end = points.last();
        const double miss = targetDefined_ ? (end - target).length() : 0.0;
        QString summary = QStringLiteral(
            "Yaw %1° · elevation %2° · flight %3 s · endpoint X %4, Y %5, Z %6.")
            .arg(qRadiansToDegrees(yaw), 0, 'f', 2)
            .arg(qRadiansToDegrees(pitch), 0, 'f', 2)
            .arg(duration, 0, 'f', 2)
            .arg(end.x(), 0, 'f', 1).arg(end.y(), 0, 'f', 1).arg(end.z(), 0, 'f', 1);
        if (targetDefined_) summary += QStringLiteral(" Ideal endpoint error: %1 units.").arg(miss, 0, 'f', 1);
        summary += QStringLiteral(" Green = launch, red = target, amber = trajectory. This is a visual planning aid, not a live-game controller.");
        ballisticsSummary_->setText(summary);
    }

    void populateLumpTable() {
        const auto &lumps = bspFile_.lumps();
        lumpTable_->setRowCount(static_cast<int>(lumps.size()));
        for (int row = 0; row < static_cast<int>(lumps.size()); ++row) {
            const auto &lump = lumps[static_cast<size_t>(row)];
            const QString records = lump.recordCount >= 0
                ? QString::number(lump.recordCount) : QStringLiteral("—");
            const QStringList columns{
                lump.name,
                QString::number(lump.length),
                records,
                QString::number(lump.version)
            };
            for (int col = 0; col < columns.size(); ++col) {
                auto *item = new QTableWidgetItem(columns[col]);
                if (col == 0) item->setToolTip(
                    QStringLiteral("Offset: %1 | FourCC: 0x%2")
                        .arg(lump.offset)
                        .arg(lump.fourCC, 8, 16, QLatin1Char('0')));
                lumpTable_->setItem(row, col, item);
            }
        }
    }

    void updateMatchEnabled(QPushButton *button) {
        button->setEnabled(!reference_.empty() && !current_.empty());
    }

    void refreshImages() {
        if (referenceView_ && !reference_.empty())
            referenceView_->setPixmap(toPixmap(reference_).scaled(
                referenceView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (currentView_ && !current_.empty())
            currentView_->setPixmap(toPixmap(current_).scaled(
                currentView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (resultView_ && !matchImage_.empty())
            resultView_->setPixmap(toPixmap(matchImage_).scaled(
                resultView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }

    void findMatches() {
        if (reference_.empty() || current_.empty()) return;
        cv::Mat refGray, curGray;
        cv::cvtColor(reference_, refGray, cv::COLOR_BGR2GRAY);
        cv::cvtColor(current_, curGray, cv::COLOR_BGR2GRAY);
        auto orb = cv::ORB::create(2500);
        std::vector<cv::KeyPoint> refPoints, curPoints;
        cv::Mat refDescriptors, curDescriptors;
        orb->detectAndCompute(refGray, cv::noArray(), refPoints, refDescriptors);
        orb->detectAndCompute(curGray, cv::noArray(), curPoints, curDescriptors);
        if (refDescriptors.empty() || curDescriptors.empty()) {
            QMessageBox::information(this, QStringLiteral("No features"),
                QStringLiteral("Not enough visual features were found in one of the images."));
            return;
        }

        cv::BFMatcher matcher(cv::NORM_HAMMING);
        std::vector<std::vector<cv::DMatch>> pairs;
        matcher.knnMatch(refDescriptors, curDescriptors, pairs, 2);
        std::vector<cv::DMatch> good;
        for (const auto &pair : pairs) {
            if (pair.size() == 2 && pair[0].distance < 0.75F * pair[1].distance)
                good.push_back(pair[0]);
        }
        std::sort(good.begin(), good.end(), [](const auto &a, const auto &b) {
            return a.distance < b.distance;
        });
        if (good.size() > 250) good.resize(250);

        std::vector<cv::Point2f> referencePoints;
        std::vector<cv::Point2f> screenshotPoints;
        referencePoints.reserve(good.size());
        screenshotPoints.reserve(good.size());
        for (const auto &match : good) {
            referencePoints.push_back(refPoints[static_cast<size_t>(match.queryIdx)].pt);
            screenshotPoints.push_back(curPoints[static_cast<size_t>(match.trainIdx)].pt);
        }

        std::vector<cv::DMatch> displayMatches = good;
        int inlierCount = -1;
        bool homographyFound = false;
        if (good.size() >= 4) {
            cv::Mat inlierMask;
            const cv::Mat homography = cv::findHomography(
                referencePoints, screenshotPoints, cv::RANSAC, 3.0, inlierMask);
            if (!homography.empty() && !inlierMask.empty()) {
                homographyFound = true;
                inlierCount = 0;
                displayMatches.clear();
                for (size_t i = 0; i < good.size(); ++i) {
                    if (inlierMask.at<uchar>(static_cast<int>(i)) != 0) {
                        displayMatches.push_back(good[i]);
                        ++inlierCount;
                    }
                }
            }
        }
        if (displayMatches.empty()) displayMatches = good;

        cv::drawMatches(reference_, refPoints, current_, curPoints, displayMatches, matchImage_,
                        cv::Scalar::all(-1), cv::Scalar::all(-1), {},
                        cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        QString summary = QStringLiteral(
            "ORB baseline — keypoints: reference %1, screenshot %2; ratio-test matches: %3.")
            .arg(static_cast<int>(refPoints.size())).arg(static_cast<int>(curPoints.size())).arg(static_cast<int>(good.size()));
        if (homographyFound) {
            summary += QStringLiteral(
                " RANSAC homography inliers: %1/%2. This rejects many inconsistent matches, "
                "but it is not a 3D camera-pose estimate.")
                .arg(inlierCount).arg(static_cast<int>(good.size()));
        } else {
            summary += QStringLiteral(
                " No stable planar homography found. This can happen when there are too few matches "
                "or the scene is strongly non-planar.");
        }
        summary += QStringLiteral(
            " Camera localization still requires calibrated intrinsics and verified 2D-to-3D landmarks.");
        matchSummary_->setText(summary);
        resultView_->setToolTip(summary);
        refreshImages();
        statusBar()->showMessage(QStringLiteral("Image matching complete."));
    }

    QLabel *referenceView_ = nullptr;
    QLabel *currentView_ = nullptr;
    QLabel *resultView_ = nullptr;
    QLabel *matchSummary_ = nullptr;
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
    int pickMode_ = 0;
    bool targetDefined_ = false;
    bspvision::BspFile bspFile_;
    cv::Mat reference_;
    cv::Mat current_;
    cv::Mat matchImage_;
};

int main(int argc, char *argv[]) {
    QApplication app(argc, argv);
    MainWindow window;
    window.show();
    return app.exec();
}
