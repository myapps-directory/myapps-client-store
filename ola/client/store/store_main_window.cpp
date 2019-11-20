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
    if (index.data().canConvert<QString>()) {
        const QString text = qvariant_cast<QString>(index.data());

        painter->save();

        painter->setRenderHint(QPainter::Antialiasing, true);
        if (option.state & QStyle::State_Selected) {
            painter->fillRect(option.rect, option.palette.highlight());
        }else if (option.state & QStyle::State_MouseOver) {
            painter->setPen(option.palette.highlight().color());
            //painter->drawRect(QRect(option.rect.x(), option.rect.y(), option.rect.width()-1, option.rect.height()-1));
        }

        painter->setBrush(QBrush(QColor(index.row() % 255, 0, 0)));
        painter->drawEllipse(option.rect);

        painter->drawText(QPoint(option.rect.x() + option.rect.width() / 2, option.rect.y() + option.rect.height() / 2), text);

        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
QSize ItemDelegate::sizeHint(const QStyleOptionViewItem& option,
    const QModelIndex&                                   index) const
{
    return QSize(200, 100);
}

MainWindow::MainWindow(QWidget* parent)
    : QMainWindow(parent)
    , pimpl_(solid::make_pimpl<Data>())
{
    pimpl_->store_form_.setupUi(this);
    pimpl_->list_form_.setupUi(pimpl_->store_form_.listWidget);
    pimpl_->list_form_.listView->viewport()->setAttribute(Qt::WA_Hover, true);
    pimpl_->list_form_.listView->setFlow(QListView::Flow::LeftToRight);
    pimpl_->list_form_.listView->setViewMode(QListView::IconMode);
    pimpl_->list_form_.listView->setMovement(QListView::Static);
    pimpl_->list_form_.listView->setResizeMode(QListView::Adjust);
    pimpl_->list_form_.listView->setGridSize(QSize(200, 100));
    pimpl_->list_form_.listView->setWordWrap(true);
    pimpl_->list_form_.listView->setWrapping(true);
    pimpl_->list_form_.listView->setModel(&pimpl_->list_model_);
    pimpl_->list_form_.listView->setItemDelegate(&pimpl_->list_delegate_);
    setWindowFlags(Qt::Drawer);

    installEventFilter(this);

    connect(this, SIGNAL(offlineSignal(bool)), this, SLOT(onOffline(bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(closeSignal()), this, SLOT(close()), Qt::QueuedConnection);

    pimpl_->store_form_.itemWidget->hide();
}

MainWindow::~MainWindow() {}

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
