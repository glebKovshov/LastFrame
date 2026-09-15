#pragma once

#include <QDialog>
#include <QRect>

class QKeyEvent;
class QMouseEvent;
class QPaintEvent;

namespace LastFrame::UI {

class RegionSelector final : public QDialog {
public:
    explicit RegionSelector(const QRect& monitorGeometry, QWidget* parent = nullptr);

    [[nodiscard]] QRect selectedRegion() const noexcept { return selection_; }

protected:
    void keyPressEvent(QKeyEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void paintEvent(QPaintEvent* event) override;

private:
    [[nodiscard]] QRect normalizedSelection() const;

    QRect monitorGeometry_;
    QRect selection_;
    QPoint anchor_;
    bool selecting_ = false;
};

} // namespace LastFrame::UI
