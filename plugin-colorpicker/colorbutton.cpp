#include "colorbutton.h"
#include <QPainter>


void ColorButton::paintEvent(QPaintEvent *event)
{
    Q_UNUSED(event)
    QToolButton::paintEvent(event);
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing, true);
    qreal minDimension = std::min(size().width(), size().height()) / 2;
    qreal minIconSize  = std::min(iconSize().width(), iconSize().height()) / 2;
    qreal radio = std::min(minDimension, minIconSize) - 3;
    QPainterPath path;
    path.addEllipse(QRectF(rect()).center(), radio, radio);
    painter.fillPath(path, color);
    painter.drawPath(path);
}


void ColorButton::setColor(QColor color)
{
    this->color = color;
    repaint();
}
