#pragma once
#include <QColor>
#include <QImage>
#include <QRect>
#include <cmath>
// Pick the higher-contrast ink from the pixels actually behind the title.
inline QColor CoverTitleInk(const QImage& image, const QRect& titleArea) {
 const auto area=titleArea.intersected(image.rect());
 if(area.isEmpty())return QColor(Qt::white);
 double luminance=0;int samples=0;
 const auto linear=[](double c){return c<=0.04045?c/12.92:std::pow((c+0.055)/1.055,2.4);};
 for(int y=area.top();y<=area.bottom();y+=qMax(1,area.height()/16))
  for(int x=area.left();x<=area.right();x+=qMax(1,area.width()/24)){
   const QColor c=image.pixelColor(x,y);
   luminance+=0.2126*linear(c.redF())+0.7152*linear(c.greenF())+0.0722*linear(c.blueF());++samples;
  }
 return samples && luminance/samples>0.179 ? QColor("#111111") : QColor(Qt::white);
}
