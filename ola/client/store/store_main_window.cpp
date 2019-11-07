#include "store_main_window.hpp"
#include "ui_store_form.h"
#include <QKeyEvent>

#include "solid/system/log.hpp"

namespace ola {
namespace client {
namespace store {

namespace {
const solid::LoggerT logger("ola::client::store::widget");
}

struct MainWindow::Data {
    Ui::StoreForm             form_;
};

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , pimpl_(solid::make_pimpl<Data>())
{
    pimpl_->form_.setupUi(this);
    setWindowFlags(Qt::Drawer);

    installEventFilter(this);

    connect(this, SIGNAL(offlineSignal(bool)), this, SLOT(onOffline(bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(closeSignal()), this, SLOT(close()), Qt::QueuedConnection);
}

MainWindow::~MainWindow() {}

void MainWindow::setUser(const std::string& _user)
{
    solid_log(logger, Info, "" << _user);
}

void MainWindow::start()
{
    this->show();
}



void MainWindow::onOffline(bool _b)
{
    solid_log(logger, Verbose, "" << _b);
}

void MainWindow::closeEvent(QCloseEvent*)
{
    QApplication::quit();
}

bool MainWindow::eventFilter(QObject* obj, QEvent* event)
{
    if (event->type() == QEvent::KeyPress) {
        QKeyEvent* key = static_cast<QKeyEvent*>(event);
        if ((key->key() == Qt::Key_Enter) || (key->key() == Qt::Key_Return)) {
        } else {
            return QObject::eventFilter(obj, event);
        }
        return true;
    } else {
        return QObject::eventFilter(obj, event);
    }
    return false;
}

} //namespace store
} //namespace client
} //namespace ola
