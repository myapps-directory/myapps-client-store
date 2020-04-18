#include "store_main_window.hpp"
#include "ui_about_form.h"
#include "ui_account_form.h"
#include "ui_item_form.h"
#include "ui_list_form.h"
#include "ui_store_form.h"
#include <QAction>
#include <QApplication>
#include <QDesktopWidget>
#include <QComboBox>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QTextLayout>
#include <QToolBar>
#include <QToolButton>
#include <algorithm>
#include <stack>
#include <vector>

#include "solid/system/log.hpp"

using namespace std;

namespace ola {
namespace client {
namespace store {

namespace {
const solid::LoggerT logger("ola::client::store::widget");
constexpr int        g_image_width       = 384;
constexpr int        g_image_height      = 216;
constexpr int        g_item_width        = 384;
constexpr int        g_item_height       = 344;
constexpr int        item_column_count = 3;
constexpr int        item_row_count    = 2;

using HistoryFunctionT = std::function<void()>;
using HistoryStackT    = std::stack<std::pair<const QWidget*, HistoryFunctionT>>;
} //namespace

struct MainWindow::Data {
    Ui::StoreForm   store_form_;
    Ui::ListForm    list_form_;
    Ui::ItemForm    item_form_;
    Ui::AccountForm account_form_;
    Ui::AboutForm   about_form_;
    int             current_item_ = -1;
    QAction         back_action_;
    QAction         home_action_;
    QAction         account_action_;
    QAction         about_action_;
    QToolBar        tool_bar_;
    QMenu           config_menu_;
    HistoryStackT   history_;
    QImage          current_image_;
    int             dpi_x_   = QApplication::desktop()->logicalDpiX();
    int             dpi_y_   = QApplication::desktop()->logicalDpiY();
    double          scale_x_ = double(dpi_x_) / 120.0; //173.0 / double(dpi_x_);
    double          scale_y_ = double(dpi_y_) / 120.0; //166.0 / double(dpi_y_);
    Sizes           sizes_{scale_x_, scale_y_, g_image_width, g_image_height, g_item_width, g_item_height};
    ItemDelegate    list_delegate_{sizes_};
    ListModel       list_model_;

    Data(Engine& _rengine, MainWindow* _pw)
        : list_model_(_rengine, sizes_)
        , back_action_(QIcon(":/images/back.png"), tr("&Back"), _pw)
        , home_action_(QIcon(":/images/home.png"), tr("&Home"), _pw)
        , account_action_(QIcon(":/images/account.png"), tr("&Account"), _pw)
        , about_action_(QIcon(":/images/about.png"), tr("A&bout"), _pw)
        , tool_bar_(_pw)
        , config_menu_(_pw)
    {
    }

    Engine& engine()
    {
        return list_model_.engine();
    }

    void showWidget(QWidget* _pw)
    {
        if (store_form_.accountWidget != _pw) {
            store_form_.accountWidget->hide();
        }
        if (store_form_.imageWidget != _pw) {
            store_form_.imageWidget->hide();
        }
        if (store_form_.listWidget != _pw) {
            store_form_.listWidget->hide();
        }
        if (store_form_.itemWidget != _pw) {
            store_form_.itemWidget->hide();
        }
        if (store_form_.aboutWidget != _pw) {
            store_form_.aboutWidget->hide();
        }
        _pw->show();
    }

    template <class F>
    HistoryFunctionT& historyPush(const QWidget *_pw, F _f)
    {
        if (history_.empty() || _pw == nullptr || history_.top().first != _pw) {
            history_.emplace(_pw, _f);
        } else {
            history_.top().second = std::move(_f);
        }
        return history_.top().second;
    }
};

void ListItem::paint(QPainter* painter, const Sizes& _rszs, const QStyleOptionViewItem& option, const QPixmap& _acquired_pix, const QPixmap& _owned_pix, const QPixmap& _acquired_owned_pix) const
{
    painter->setRenderHint(QPainter::Antialiasing, true);

    QColor pen_color = painter->pen().color();

    if (option.state & QStyle::State_Selected) {
        painter->fillRect(option.rect, option.palette.highlight());
    } else if (option.state & QStyle::State_MouseOver) {
        pen_color = option.palette.highlight().color();
        painter->setPen(pen_color);
    }
    painter->translate(option.rect.x(), option.rect.y());
    painter->setBrush(QBrush(QColor(100, 0, 0)));
    painter->drawImage(QPoint((_rszs.image_width_ - image_.width()) / 2, (_rszs.image_height_ - image_.height()) / 2), image_);

    QFont base_font = painter->font();

    int lineSpacing = _rszs.image_height_;
    {
        QFont font = painter->font();
        font.setBold(true);
        font.setPointSize(font.pointSize() + (font.pointSize() * 50) / 100);
        painter->setFont(font);

        QFontMetrics fontMetrics = painter->fontMetrics();
        QTextLayout  layout(this->name_, painter->font());
        layout.beginLayout();
        QTextLine line = layout.createLine();

        if (line.isValid()) {
            line.setLineWidth(_rszs.item_width_);
            QString lastLine       = this->name_;
            QString elidedLastLine = fontMetrics.elidedText(lastLine, Qt::ElideRight, _rszs.item_width_);

            painter->drawText(QPoint(0, 0 + fontMetrics.ascent() + lineSpacing), elidedLastLine);
            painter->setPen(option.palette.highlight().color());
        }
        lineSpacing += fontMetrics.lineSpacing();

        layout.endLayout();
    }
    {
        const int pix_x = _rszs.image_width_ - 32;
        if (acquired_ && owned_) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _acquired_owned_pix);
        } else if (acquired_) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _acquired_pix);
        } else if (owned_) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _owned_pix);
        }
    }

    painter->setFont(base_font);
    {
        QFont font = painter->font();
        font.setPointSize(font.pointSize() - (font.pointSize() * 25) / 100);
        painter->setFont(font);

        QFontMetrics fontMetrics = painter->fontMetrics();

        QTextLayout layout(this->company_, painter->font());
        layout.beginLayout();
        QTextLine line = layout.createLine();

        if (line.isValid()) {
            line.setLineWidth(_rszs.item_width_);
            QString lastLine       = this->company_;
            QString elidedLastLine = fontMetrics.elidedText(lastLine, Qt::ElideRight, _rszs.item_width_);
            painter->setPen(QColor(180, 180, 180));
            painter->drawText(QPoint(0, fontMetrics.ascent() + lineSpacing), elidedLastLine);
            painter->setPen(pen_color);
        }
        lineSpacing += fontMetrics.lineSpacing();

        layout.endLayout();
    }
    painter->setFont(base_font);
    {
        QFontMetrics fontMetrics  = painter->fontMetrics();
        int          line_spacing = fontMetrics.lineSpacing();

        QTextLayout layout(this->brief_, painter->font());
        layout.beginLayout();
        int y = 0;

        forever
        {
            QTextLine line = layout.createLine();

            if (!line.isValid())
                break;

            line.setLineWidth(_rszs.item_width_);
            int nextLineY = y + line_spacing;

            if ((_rszs.item_height_ - lineSpacing) >= nextLineY + line_spacing) {
                line.draw(painter, QPoint(0, lineSpacing + y));
                y = nextLineY;
            } else {
                QString lastLine       = brief_.mid(line.textStart());
                QString elidedLastLine = fontMetrics.elidedText(lastLine, Qt::ElideRight, _rszs.item_width_);
                painter->drawText(QPoint(0, fontMetrics.ascent() + lineSpacing + y), elidedLastLine);
                line = layout.createLine();
                break;
            }
        }
        layout.endLayout();
    }
}

ListModel::ListModel(Engine& _rengine, const Sizes &_rsizes, QObject* parent)
    : QAbstractListModel(parent)
    , rsizes_(_rsizes)
    , rengine_(_rengine)
{
    connect(this, SIGNAL(newItemSignal()), this, SLOT(newItemsSlot()), Qt::QueuedConnection);
}

void ListModel::prepareAndPushItem(
    const size_t        _index,
    const size_t        _count,
    const std::string&  _name,
    const std::string&  _company,
    const std::string&  _brief,
    const vector<char>& _image,
    const bool          _acquired,
    const bool          _owned,
    const bool          _default)
{
    //called on pool thread
    ListItem item;
    item.engine_index_ = _index;
    item.brief_        = QString::fromStdString(_brief);
    item.company_      = QString::fromStdString(_company);
    item.name_         = QString::fromStdString(_name);
    item.owned_        = _owned;
    item.acquired_     = _acquired;
    item.default_      = _default;
    QImage img;
    if (img.loadFromData(reinterpret_cast<const uchar*>(_image.data()), _image.size())) {
        item.image_ = img.scaled(QSize(rsizes_.image_width_, rsizes_.image_height_), Qt::KeepAspectRatio, Qt::SmoothTransformation);
    }

    {
        std::lock_guard lock(mutex_);
        push_item_dq_.emplace_back(std::move(item));
        ++push_item_count_;
        if (push_item_count_ == _count) {
            engine_fetch_index_ += _count;
            push_item_count_ = 0;
            emit newItemSignal();
        }
    }
}

void ListModel::prepareAndPushItem(
    const size_t _index,
    const size_t _count)
{
    std::lock_guard lock(mutex_);
    ++push_item_count_;
    if (push_item_count_ == _count) {
        engine_fetch_index_ += _count;
        push_item_count_ = 0;
        emit newItemSignal();
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
            requested_more_ = rengine_.requestMore(engine_fetch_index_, fetch_count_);
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
        requested_more_ = rengine_.requestMore(engine_fetch_index_, fetch_count_);
        return;
    }

    std::sort(pop_item_dq_.begin(), pop_item_dq_.end(), [](const ListItem& _i1, const ListItem& _i2) { return _i1.engine_index_ < _i2.engine_index_; });
    rengine_.onModelFetchedItems(count_, engine_fetch_index_, pop_item_dq_.size());

    beginInsertRows(QModelIndex(), count_, count_ + pop_item_dq_.size() - 1);

    for (auto&& item : pop_item_dq_) {
        item_dq_.emplace_back(std::move(item));
    }

    request_more_index_ = count_ + (pop_item_dq_.size() / 2);
    requested_more_     = false;
    count_ += pop_item_dq_.size();

    endInsertRows();
    pop_item_dq_.clear();
    emit numberPopulated(count_);
}

ItemDelegate::ItemDelegate(const Sizes& _rsizes)
    : rsizes_(_rsizes)
    , acquired_pix_(":/images/acquired.png")
    , owned_pix_(":/images/owned.png")
    , acquired_owned_pix_(":/images/acquired_owned.png")
{
}

void ItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    if (index.data().canConvert<const ListItem*>()) {
        const ListItem* pitem = qvariant_cast<const ListItem*>(index.data());

        painter->save();

        pitem->paint(painter, rsizes_, option, acquired_pix_, owned_pix_, acquired_owned_pix_);

        painter->restore();
    } else {
        QStyledItemDelegate::paint(painter, option, index);
    }
}
QSize ItemDelegate::sizeHint(const QStyleOptionViewItem& option,
    const QModelIndex&                                   index) const
{
    return QSize(rsizes_.item_width_, rsizes_.item_height_);
}

MainWindow::MainWindow(Engine& _rengine, QWidget* parent)
    : QMainWindow(parent)
    , pimpl_(solid::make_pimpl<Data>(_rengine, this))
{
    qRegisterMetaType<VectorPairStringT>("VectorPairStringT");

    setWindowFlags(windowFlags() & (~Qt::WindowMaximizeButtonHint));

    pimpl_->store_form_.setupUi(this);
    pimpl_->list_form_.setupUi(pimpl_->store_form_.listWidget);
    pimpl_->item_form_.setupUi(pimpl_->store_form_.itemWidget);
    pimpl_->about_form_.setupUi(pimpl_->store_form_.aboutWidget);

    pimpl_->account_form_.setupUi(pimpl_->store_form_.accountWidget);
    pimpl_->list_form_.listView->viewport()->setAttribute(Qt::WA_Hover, true);
    pimpl_->list_form_.listView->setFlow(QListView::Flow::LeftToRight);
    pimpl_->list_form_.listView->setViewMode(QListView::IconMode);
    pimpl_->list_form_.listView->setMovement(QListView::Static);
    pimpl_->list_form_.listView->setResizeMode(QListView::Adjust);
    pimpl_->list_form_.listView->setGridSize(QSize(pimpl_->sizes_.item_width_, pimpl_->sizes_.item_height_));
    pimpl_->list_form_.listView->setWordWrap(true);
    pimpl_->list_form_.listView->setWrapping(true);
    pimpl_->list_form_.listView->setModel(&pimpl_->list_model_);
    pimpl_->list_form_.listView->setItemDelegate(&pimpl_->list_delegate_);

    pimpl_->about_form_.image_label->setPixmap(QPixmap(":/images/ola_store_bag.png"));

    //setWindowFlags(Qt::Drawer);
    {
        int   aElements[2] = {COLOR_WINDOW, COLOR_ACTIVECAPTION};
        DWORD aOldColors[2];

        aOldColors[0] = GetSysColor(aElements[0]);

        QPalette     pal = palette();
        const QColor win_color(GetRValue(aOldColors[0]), GetGValue(aOldColors[0]), GetBValue(aOldColors[0]));
        // set black background
        pal.setColor(QPalette::Window, win_color);
        setAutoFillBackground(true);
        setPalette(pal);
    }
    installEventFilter(this);

    connect(this, SIGNAL(offlineSignal(bool)), this, SLOT(onOffline(bool)), Qt::QueuedConnection);
    connect(this, SIGNAL(closeSignal()), this, SLOT(close()), Qt::QueuedConnection);
    connect(pimpl_->list_form_.listView, SIGNAL(doubleClicked(const QModelIndex&)), this, SLOT(onItemDoubleClicked(const QModelIndex&)));
    connect(pimpl_->item_form_.acquire_button, SIGNAL(toggled(bool)), this, SLOT(onAquireButtonToggled(bool)));

    connect(this, SIGNAL(itemData(int, QString, QString)), this, SLOT(itemDataSlot(int, const QString&, const QString&)), Qt::QueuedConnection);

    connect(this, SIGNAL(itemMedia(int, VectorPairStringT)), this, SLOT(itemMediaSlot(int, const VectorPairStringT&)), Qt::QueuedConnection);
    connect(this, SIGNAL(itemAcquire(int, bool)), this, SLOT(itemAcquireSlot(int, bool)), Qt::QueuedConnection);

    connect(pimpl_->item_form_.media_list_widget, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(imageDoubleClicked(QListWidgetItem*)));

    connect(&pimpl_->home_action_, &QAction::triggered, this, &MainWindow::goHomeSlot);
    connect(&pimpl_->account_action_, &QAction::triggered, this, &MainWindow::goAccountSlot);
    connect(&pimpl_->back_action_, &QAction::triggered, this, &MainWindow::goBackSlot);
    connect(&pimpl_->about_action_, &QAction::triggered, this, &MainWindow::goAboutSlot);

    pimpl_->tool_bar_.setMovable(false);
    pimpl_->tool_bar_.setFixedHeight(38 * pimpl_->scale_y_);
    pimpl_->tool_bar_.setIconSize(QSize(32 * pimpl_->scale_x_, 32 * pimpl_->scale_y_));
    pimpl_->tool_bar_.setStyleSheet("QToolBar { border: 0px }");
    pimpl_->tool_bar_.setMovable(false);
    this->addToolBar(&pimpl_->tool_bar_);

    QToolButton* ptoolbutton = new QToolButton;

    ptoolbutton->setIcon(QIcon(":/images/config.png"));
    ptoolbutton->setMenu(&pimpl_->config_menu_);
    ptoolbutton->setPopupMode(QToolButton::ToolButtonPopupMode::InstantPopup);

    pimpl_->tool_bar_.addAction(&pimpl_->back_action_);
    pimpl_->tool_bar_.addAction(&pimpl_->home_action_);
    QComboBox* psearchcombo = new QComboBox;
    psearchcombo->setMinimumWidth(300);
    psearchcombo->setEditable(true);
    psearchcombo->setEnabled(false);
    psearchcombo->setEditText("All");
    psearchcombo->setToolTip(tr("Search"));

    QWidget* empty = new QWidget();
    empty->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    
    pimpl_->tool_bar_.addSeparator();
    pimpl_->tool_bar_.addWidget(psearchcombo);
    pimpl_->tool_bar_.addWidget(empty);
    pimpl_->tool_bar_.addSeparator();
    pimpl_->tool_bar_.addWidget(ptoolbutton);
    pimpl_->tool_bar_.addSeparator();
    pimpl_->tool_bar_.addAction(&pimpl_->about_action_);

    pimpl_->config_menu_.addAction(&pimpl_->account_action_);

    pimpl_->showWidget(pimpl_->store_form_.listWidget);

    resize(QSize((pimpl_->sizes_.item_width_ * item_column_count + 60), (pimpl_->sizes_.item_height_ * item_row_count + 133)));

    pimpl_->history_.emplace(
        pimpl_->store_form_.listWidget,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.listWidget);
        });
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

void MainWindow::onItemDoubleClicked(const QModelIndex& _index)
{
    solid_log(logger, Verbose, "" << _index.row());

    pimpl_->historyPush(
        nullptr,
        [this, index = _index.row()]() {
            showItem(index);
            pimpl_->showWidget(pimpl_->store_form_.itemWidget);
        })();
}

void MainWindow::showItem(int _index)
{
    const QSize img_size{pimpl_->sizes_.image_width_, pimpl_->sizes_.image_height_};

    pimpl_->current_item_ = _index;
    auto& item            = pimpl_->list_model_.item(pimpl_->current_item_);
    pimpl_->item_form_.frame->setFixedHeight(pimpl_->sizes_.image_height_);
    pimpl_->item_form_.image_label->setFixedSize(img_size);
    pimpl_->item_form_.image_label->setPixmap(QPixmap::fromImage(item.image_));
    pimpl_->item_form_.name_label->setText(item.name_);
    pimpl_->item_form_.company_label->setText(item.company_);
    pimpl_->item_form_.brief_label->setText(item.brief_);

    pimpl_->item_form_.media_list_widget->hide();
    pimpl_->item_form_.media_list_widget->clear();

    pimpl_->item_form_.acquire_button->setChecked(item.acquired_);
    pimpl_->item_form_.acquire_button->setEnabled(!item.default_);

    if (item.acquired_ && item.owned_) {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired_owned.png"));
    } else if (item.acquired_ || item.default_) {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired.png"));
    } else if (item.owned_) {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/owned.png"));
    } else {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/empty.png"));
    }

    pimpl_->engine().fetchItemData(
        item.engine_index_,
        [this, index = _index](const std::string& _description, const std::string& _release) {
            //called on another thread - need to move the data onto GUI thread
            emit itemData(index, QString::fromStdString(_description), QString::fromStdString(_release));
        });
    if (item.media_vec_.empty()) {
        pimpl_->engine().fetchItemMedia(
            item.engine_index_,
            [this, index = _index](const std::vector<std::pair<std::string, std::string>>& _media_vec) {
                //called on another thread - need to move the data onto GUI thread
                QVector<QPair<QString, QString>> media_vec;

                for (auto& m : _media_vec) {
                    media_vec.append(QPair<QString, QString>(QString::fromStdString(m.first), QString::fromStdString(m.second)));
                }

                emit itemMedia(index, media_vec);
            });
    } else {
        showMediaThumbnails(_index);
    }
}

void MainWindow::itemDataSlot(int _index, const QString& _description, const QString& _release)
{
    pimpl_->item_form_.description_label->setText(_description);
    pimpl_->item_form_.release_label->setText(_release);
}

void MainWindow::itemMediaSlot(int _index, const VectorPairStringT& _rmedia_vec)
{
    if (!_rmedia_vec.empty()) {
        auto& item      = pimpl_->list_model_.item(_index);
        item.media_vec_ = _rmedia_vec;
        showMediaThumbnails(_index);
    }
}

void MainWindow::itemAcquireSlot(int _index, bool _acquired)
{
    auto& item = pimpl_->list_model_.item(_index);

    item.acquired_ = _acquired;

    if (_index == pimpl_->current_item_) {
        pimpl_->item_form_.acquire_button->setChecked(_acquired);
        pimpl_->item_form_.acquire_button->setEnabled(!item.default_);

        if (item.acquired_ && item.owned_) {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired_owned.png"));
        } else if (item.acquired_ || item.default_) {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired.png"));
        } else if (item.owned_) {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/owned.png"));
        } else {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/empty.png"));
        }
    }
}
namespace {
struct ThumbnailItem : QListWidgetItem {
    size_t index_;
    ThumbnailItem(const size_t _index)
        : QListWidgetItem("")
        , index_(_index)
    {
    }
};
} // namespace

void MainWindow::showMediaThumbnails(int _index)
{
    auto& item = pimpl_->list_model_.item(_index);



    if (item.media_vec_.size()) {
        const QSize thumb_size{pimpl_->sizes_.image_width_, pimpl_->sizes_.image_height_};

        pimpl_->item_form_.media_list_widget->setIconSize(thumb_size);
        pimpl_->item_form_.media_list_widget->setGridSize(thumb_size);
        pimpl_->item_form_.media_list_widget->setMinimumHeight(pimpl_->sizes_.image_height_ + 20);
        bool   has_image = false;
        size_t index     = 0;
        for (const auto& media : item.media_vec_) {
            QImage image;
            if (image.load(media.first)) {
                auto* pitem = new ThumbnailItem(index);
                pitem->setData(Qt::DecorationRole, QPixmap::fromImage(image.scaled(thumb_size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                pitem->setSizeHint(thumb_size + QSize(4, 4));
                pimpl_->item_form_.media_list_widget->addItem(pitem);
                has_image = true;
            } else {
                solid_log(logger, Warning, "Failed loading media: " << media.first.toStdString());
            }
            ++index;
        }
        if (has_image) {
            pimpl_->item_form_.media_list_widget->show();
        }
    } else {
        pimpl_->item_form_.media_list_widget->hide();
    }
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

void MainWindow::onAquireButtonToggled(bool _checked)
{
    const auto& item = pimpl_->list_model_.item(pimpl_->current_item_);

    pimpl_->engine().acquireItem(
        item.engine_index_,
        _checked,
        [this, index = pimpl_->current_item_](bool _acquired) {
            emit itemAcquire(index, _acquired);
        });
}

void MainWindow::imageDoubleClicked(QListWidgetItem* _item)
{
    auto* pthumb = static_cast<ThumbnailItem*>(_item);

    pimpl_->historyPush(
        nullptr,
        [this, item_index = pimpl_->current_item_, image_index = pthumb->index_]() {
            const auto&    item = pimpl_->list_model_.item(item_index);
            const QString& path = item.media_vec_[image_index].second;

            if (pimpl_->current_image_.load(path)) {
                const int w = pimpl_->store_form_.centralwidget->width();
                const int h = pimpl_->store_form_.centralwidget->height();
                pimpl_->store_form_.image_label->setPixmap(QPixmap::fromImage(pimpl_->current_image_.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                pimpl_->showWidget(pimpl_->store_form_.imageWidget);
            }
        })();
}

void MainWindow::resizeEvent(QResizeEvent* event)
{
    if (pimpl_->store_form_.imageWidget->isVisible()) {
        const int w = pimpl_->store_form_.centralwidget->width();
        const int h = pimpl_->store_form_.centralwidget->height();
        pimpl_->store_form_.image_label->setPixmap(QPixmap::fromImage(pimpl_->current_image_.scaled(QSize(w, h), Qt::KeepAspectRatio, Qt::SmoothTransformation)));             
    }
}

void MainWindow::goHomeSlot(bool)
{
    pimpl_->historyPush(
        pimpl_->store_form_.listWidget,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.listWidget);
        })();
}

void MainWindow::goAccountSlot(bool)
{
    pimpl_->historyPush(
        pimpl_->store_form_.accountWidget,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.accountWidget);
        })();
}
void MainWindow::goBackSlot(bool)
{
    if (!pimpl_->history_.empty()) {
        if (pimpl_->history_.size() > 1) {
            pimpl_->history_.pop();
        }
        if (!pimpl_->history_.empty()) {
            pimpl_->history_.top().second();
        }
    }
}

void MainWindow::goAboutSlot(bool)
{
    pimpl_->historyPush(
        pimpl_->store_form_.aboutWidget,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.aboutWidget);
        })();
}

} //namespace store
} //namespace client
} //namespace ola
