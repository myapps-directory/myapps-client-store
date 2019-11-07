#pragma once

#include "solid/system/pimpl.hpp"
#include <QMainWindow>
#include <functional>
#include <string>

namespace ola {
namespace client {
namespace store {

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(QWidget* parent = 0);
    ~MainWindow();

    void setUser(const std::string& _user);

    void start();
signals:
    void closeSignal();
    void offlineSignal(bool);
private slots:
    void onOffline(bool);

private:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct Data;
    solid::PimplT<Data> pimpl_;
};
} //namespace auth
} //namespace client
} //namespace ola
