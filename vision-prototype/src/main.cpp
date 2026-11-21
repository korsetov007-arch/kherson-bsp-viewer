#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QImage>
#include <QLabel>
#include <QMainWindow>
#include <QMessageBox>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QStatusBar>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QWidget>

#include <opencv2/core.hpp>
#include <opencv2/features2d.hpp>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
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
}

class MainWindow final : public QMainWindow {
public:
    MainWindow() {
        setWindowTitle(QStringLiteral("BSP Vision — visual matching prototype"));
        resize(1280, 820);
        auto *root = new QWidget(this);
        auto *layout = new QVBoxLayout(root);
        auto *controls = new QHBoxLayout();
        auto *loadReference = new QPushButton(QStringLiteral("Load reference image…"), root);
        auto *loadCurrent = new QPushButton(QStringLiteral("Load game screenshot…"), root);
        auto *match = new QPushButton(QStringLiteral("Find visual matches"), root);
        match->setEnabled(false);
        controls->addWidget(loadReference);
        controls->addWidget(loadCurrent);
        controls->addWidget(match);
        controls->addStretch();
        layout->addLayout(controls);

        auto *hint = new QLabel(QStringLiteral(
            "Offline prototype: compare two images and inspect feature matches. "
            "No game-process access, input automation, or aiming controls."), root);
        hint->setWordWrap(true);
        layout->addWidget(hint);

        auto *images = new QHBoxLayout();
        referenceView_ = makeImageView(QStringLiteral("Reference image not loaded"), root);
        currentView_ = makeImageView(QStringLiteral("Screenshot not loaded"), root);
        images->addWidget(wrap(referenceView_, root), 1);
        images->addWidget(wrap(currentView_, root), 1);
        layout->addLayout(images, 2);

        resultView_ = makeImageView(QStringLiteral("Match visualization will appear here."), root);
        layout->addWidget(wrap(resultView_, root), 2);
        setCentralWidget(root);
        statusBar()->showMessage(QStringLiteral("Load a reference image and a screenshot."));

        connect(loadReference, &QPushButton::clicked, this, [this, match] {
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose reference image"), {},
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty()) return;
            reference_ = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
            if (reference_.empty()) {
                QMessageBox::warning(this, QStringLiteral("Image error"), QStringLiteral("Could not read image: ") + path);
                return;
            }
            referenceView_->setToolTip(QFileInfo(path).fileName());
            refreshImages();
            updateMatchEnabled(match);
            statusBar()->showMessage(QStringLiteral("Reference loaded: %1 × %2").arg(reference_.cols).arg(reference_.rows));
        });

        connect(loadCurrent, &QPushButton::clicked, this, [this, match] {
            const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Choose screenshot"), {},
                QStringLiteral("Images (*.png *.jpg *.jpeg *.bmp *.webp)"));
            if (path.isEmpty()) return;
            current_ = cv::imread(path.toStdString(), cv::IMREAD_COLOR);
            if (current_.empty()) {
                QMessageBox::warning(this, QStringLiteral("Image error"), QStringLiteral("Could not read image: ") + path);
                return;
            }
            currentView_->setToolTip(QFileInfo(path).fileName());
            refreshImages();
            updateMatchEnabled(match);
            statusBar()->showMessage(QStringLiteral("Screenshot loaded: %1 × %2").arg(current_.cols).arg(current_.rows));
        });
        connect(match, &QPushButton::clicked, this, [this] { findMatches(); });
    }

protected:
    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);
        refreshImages();
    }

private:
    static QLabel *makeImageView(const QString &text, QWidget *parent) {
        auto *view = new QLabel(text, parent);
        view->setAlignment(Qt::AlignCenter);
        view->setMinimumSize(320, 220);
        view->setStyleSheet(QStringLiteral("QLabel { background: #20242b; color: #d5d9e0; border: 1px solid #454b55; }"));
        return view;
    }

    static QScrollArea *wrap(QWidget *child, QWidget *parent) {
        auto *scroll = new QScrollArea(parent);
        scroll->setWidget(child);
        scroll->setWidgetResizable(true);
        return scroll;
    }

    void updateMatchEnabled(QPushButton *button) {
        button->setEnabled(!reference_.empty() && !current_.empty());
    }

    void refreshImages() {
        if (!reference_.empty())
            referenceView_->setPixmap(toPixmap(reference_).scaled(referenceView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!current_.empty())
            currentView_->setPixmap(toPixmap(current_).scaled(currentView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
        if (!matchImage_.empty())
            resultView_->setPixmap(toPixmap(matchImage_).scaled(resultView_->size(), Qt::KeepAspectRatio, Qt::SmoothTransformation));
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
        for (const auto &pair : pairs)
            if (pair.size() == 2 && pair[0].distance < 0.75F * pair[1].distance) good.push_back(pair[0]);
        std::sort(good.begin(), good.end(), [](const auto &a, const auto &b) { return a.distance < b.distance; });
        if (good.size() > 120) good.resize(120);
        cv::drawMatches(reference_, refPoints, current_, curPoints, good, matchImage_,
                        cv::Scalar::all(-1), cv::Scalar::all(-1), {},
                        cv::DrawMatchesFlags::NOT_DRAW_SINGLE_POINTS);
        const QString summary = QStringLiteral(
            "ORB matching — reference keypoints: %1; screenshot keypoints: %2; ratio-test matches: %3. "
            "This is a visual-similarity diagnostic, not a camera-pose estimate. Pose estimation requires "
            "calibrated camera parameters and 2D-to-3D correspondences.")
            .arg(refPoints.size()).arg(curPoints.size()).arg(good.size());
        resultView_->setToolTip(summary);
        refreshImages();
        statusBar()->showMessage(summary);
    }

    QLabel *referenceView_{};
    QLabel *currentView_{};
    QLabel *resultView_{};
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
