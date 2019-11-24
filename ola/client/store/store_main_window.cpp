#include "store_main_window.hpp"
#include "ui_list_form.h"
#include "ui_store_form.h"
#include <QKeyEvent>
#include <QPainter>
#include <vector>

#include "solid/system/log.hpp"

using namespace std;

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

    Data(Engine& _rengine)
        : list_model_(_rengine)
    {
    }
};

void ListItem::paint(QPainter* painter, const QStyleOptionViewItem& option) const
{
    painter->setRenderHint(QPainter::Antialiasing, true);
    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        painter->setPen(option.palette.highlight().color());
        //painter->drawRect(QRect(option.rect.x(), option.rect.y(), option.rect.width()-1, option.rect.height()-1));
    }

    painter->setBrush(QBrush(QColor(100, 0, 0)));
    painter->drawEllipse(option.rect);

    painter->drawText(QPoint(option.rect.x() + option.rect.width() / 2, option.rect.y() + option.rect.height() / 2), name_);
}

ListModel::ListModel(Engine& _rengine, QObject* parent)
    : QAbstractListModel(parent)
    , rengine_(_rengine)
{
    connect(this, SIGNAL(newItemSignal()), this, SLOT(newItemsSlot()), Qt::QueuedConnection);
}

void ListModel::prepareAndPushItem(
    const size_t        _index,
    const size_t        _fetch_count,
    const std::string&  _name,
    const std::string&  _company,
    const std::string&  _brief,
    const vector<char>& _image)
{
    //called on pool thread
    ListItem item;
    item.brief_   = QString::fromStdString(_brief);
    item.company_ = QString::fromStdString(_company);
    item.name_    = QString::fromStdString(_name);
    {
        std::lock_guard lock(mutex_);
        push_item_dq_.emplace_back(_index, std::move(item));
        if (push_item_dq_.size() == _fetch_count) {
            emit newItemSignal();
        }
    }
}

int ListModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : count_;
}

QVariant ListModel::data(const QModelIndex& index, int role) const
{
    const ListItem* pitem = &item_dq_[index.row()];
    if (role == Qt::DisplayRole) {
        QVariant v;
        v.setValue(pitem);
        if (!requested_more_ && index.row() >= request_more_index_) {
            requested_more_ = true;
            rengine_.requestMore(count_, last_fetch_count_);
        }
        return v;
    } else if (role == Qt::ToolTipRole) {
        return QString(pitem->brief_);
    }
    return QVariant();
}

Qt::ItemFlags ListModel::flags(const QModelIndex& index) const
{
    if (!index.isValid())
        return Qt::ItemIsEnabled;

    return QAbstractListModel::flags(index) | Qt::ItemIsSelectable;
}

void ListModel::newItemsSlot()
{
    {
        std::lock_guard lock(mutex_);
        std::swap(push_item_dq_, pop_item_dq_);
    }
    if (pop_item_dq_.empty()) {
        return;
    }

    beginInsertRows(QModelIndex(), count_, count_ + pop_item_dq_.size() - 1);

    for (auto&& p : pop_item_dq_) {
        if (p.first >= item_dq_.size()) {
            item_dq_.resize(p.first + 1);
        }
        item_dq_[p.first] = std::move(p.second);
    }
    last_fetch_count_ = pop_item_dq_.size();
    request_more_index_ = count_ + (last_fetch_count_ / 2);
    requested_more_     = false;
    count_ += pop_item_dq_.size();

    endInsertRows();
    pop_item_dq_.clear();
    emit numberPopulated(count_);
}

void ItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    if (index.data().canConvert<const ListItem*>()) {
        const ListItem* pitem = qvariant_cast<const ListItem*>(index.data());

        painter->save();

        pitem->paint(painter, option);

        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
QSize ItemDelegate::sizeHint(const QStyleOptionViewItem& option,
    const QModelIndex&                                   index) const
{
    return QSize(384, 324);
}

MainWindow::MainWindow(Engine& _rengine, QWidget* parent)
    : QMainWindow(parent)
    , pimpl_(solid::make_pimpl<Data>(_rengine))
{
    pimpl_->store_form_.setupUi(this);
    pimpl_->list_form_.setupUi(pimpl_->store_form_.listWidget);
    pimpl_->list_form_.listView->viewport()->setAttribute(Qt::WA_Hover, true);
    pimpl_->list_form_.listView->setFlow(QListView::Flow::LeftToRight);
    pimpl_->list_form_.listView->setViewMode(QListView::IconMode);
    pimpl_->list_form_.listView->setMovement(QListView::Static);
    pimpl_->list_form_.listView->setResizeMode(QListView::Adjust);
    pimpl_->list_form_.listView->setGridSize(QSize(384, 324));
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

ListModel& MainWindow::model()
{
    return pimpl_->list_model_;
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
