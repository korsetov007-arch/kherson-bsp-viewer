#pragma once
#include "bsp_file.hpp"
#include <QPointF>
#include <QWidget>
#include <functional>
class QPaintEvent;class QMouseEvent;
class TopDownPicker final:public QWidget{
public:explicit TopDownPicker(QWidget* p=nullptr);void setPolygons(const QVector<bspvision::BspPolygon>&);void setMarker(double,double);void setPointCallback(std::function<void(double,double)>);
protected:void paintEvent(QPaintEvent*)override;void mousePressEvent(QMouseEvent*)override;
private:QPointF worldToScreen(double,double)const;QPointF screenToWorld(const QPointF&)const;QVector<bspvision::BspPolygon> polygons_;double minX_=-1,maxX_=1,minY_=-1,maxY_=1,markerX_=0,markerY_=0;bool hasMarker_=false;std::function<void(double,double)> pointCallback_;
};
