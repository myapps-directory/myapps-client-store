#include "store_main_window.hpp"
#include "ui_list_form.h"
#include "ui_store_form.h"
#include <QKeyEvent>
#include <QPainter>

#include "solid/system/log.hpp"

namespace ola {
namespace client {
namespace store {

namespace {
const solid::LoggerT logger("ola::client::store::widget");
} //namespace

struct MainWindow::Data {
    ListModel     list_model_;
    Ui::StoreForm store_form_;
    Ui::ListForm  list_form_;
    ItemDelegate  list_delegate_;
};

void ItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    painter->save();

    painter->setRenderHint(QPainter::Antialiasing, true);
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    }
    
    painter->setBrush(QBrush(QColor(index.row() % 255, 0, 0)));
    painter->drawEllipse(option.rect);

    painter->drawText(QPoint(option.rect.x() + option.rect.width() / 2, option.rect.y() + option.rect.height() / 2), QString("Row%1").arg(index.row() + 1));

    painter->restore();
}
QSize ItemDelegate::sizeHint(const QStyleOptionViewItem& option,
    const QModelIndex&                                   index) const
{
    return QSize(200,100);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , pimpl_(solid::make_pimpl<Data>())
{
    pimpl_->store_form_.setupUi(this);
    pimpl_->list_form_.setupUi(pimpl_->store_form_.listWidget);
    pimpl_->list_form_.listView->setFlow(QListView::Flow::LeftToRight);
    pimpl_->list_form_.listView->setWrapping(true);
    //pimpl_->list_form_.listView->setGridSize(QSize(1000,10));
    pimpl_->list_form_.listView->setModel(&pimpl_->list_model_);
    pimpl_->list_form_.listView->setItemDelegate(&pimpl_->list_delegate_);
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
    pimpl_->store_form_.itemWidget->hide();
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
