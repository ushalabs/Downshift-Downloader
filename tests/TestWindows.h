#pragma once
#include <QObject>
#include <QWidget>
#include <QEvent>
// Render the actual Qt widgets, but never present a private test queue as the user's app.
class HiddenTestWindows final:public QObject {
    bool eventFilter(QObject* object,QEvent* event) override {
        if(event->type()==QEvent::Polish)if(auto* widget=qobject_cast<QWidget*>(object);widget && widget->isWindow())widget->setAttribute(Qt::WA_DontShowOnScreen);
        return false;
    }
};
