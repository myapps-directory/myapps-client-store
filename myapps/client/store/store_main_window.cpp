#include "store_main_window.hpp"
#include "ui_about_form.h"
#include "ui_account_form.h"
#include "ui_item_form.h"
#include "ui_list_form.h"
#include "ui_store_form.h"
#include "ui_configure_form.h"
#include <QAction>
#include <QApplication>
#include <QComboBox>
#include <QImageReader>
#include <QKeyEvent>
#include <QMenu>
#include <QPainter>
#include <QTextLayout>
#include <QToolBar>
#include <QToolButton>
#include <QScrollBar>
#include <algorithm>
#include <stack>
#include <vector>
#include <chrono>
#include <iomanip>

#include "myapps/common/utility/version.hpp"

#include "solid/system/cstring.hpp"
#include "solid/system/log.hpp"

using namespace std;

namespace myapps {
namespace client {
namespace store {

namespace {
const solid::LoggerT logger("myapps::client::store::widget");
constexpr int        g_image_width     = 384;
constexpr int        g_image_height    = 216;
constexpr int        g_item_width      = 384;
constexpr int        g_item_height     = 344;
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
    Ui::ConfigureForm   configure_form_;
    int             current_item_ = -1;
    QAction         back_action_;
    QAction         home_action_;
    QAction         account_action_;
    QAction         about_action_;
    QToolBar        tool_bar_;
    QMenu           config_menu_;
    HistoryStackT   history_;
    QImage          current_image_;
    int                 dpi_x_   = QApplication::primaryScreen()->logicalDotsPerInchX();
    int                 dpi_y_   = QApplication::primaryScreen()->logicalDotsPerInchY();
    double          scale_x_ = double(dpi_x_) / 120.0; //173.0 / double(dpi_x_);
    double          scale_y_ = double(dpi_y_) / 120.0; //166.0 / double(dpi_y_);
    Sizes           sizes_{scale_x_, scale_y_, g_image_width, g_image_height, g_item_width, g_item_height};
    ItemDelegate    list_delegate_{sizes_};
    ListModel       list_model_;
    QTreeWidgetItem* pcurrent_build_parent_ = nullptr;
    QTreeWidgetItem* pcurrent_media_parent_ = nullptr;
    QString          config_current_build_;
    QString          config_current_media_;

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
        if (store_form_.configureWidget != _pw) {
            store_form_.configureWidget->hide();
        }
        _pw->show();
    }

    template <class F>
    HistoryFunctionT& historyPush(const QWidget* _pw, F _f)
    {
        if (history_.empty() || _pw == nullptr || history_.top().first != _pw) {
            history_.emplace(_pw, _f);
        } else {
            history_.top().second = std::move(_f);
        }
        return history_.top().second;
    }

    void setComboIndex(int _item_index) {
    
    }

    void configureBuildPrepareStateComboBox(const myapps::utility::AppItemEntry& _build);
    void configureMediaPrepareStateComboBox(const myapps::utility::AppItemEntry& _build);
};

void ListItem::paint(
    QPainter* painter, const Sizes& _rszs, const QStyleOptionViewItem& option,
    const QPixmap& _acquired_pix, const QPixmap& _owned_pix, const QPixmap& _review_pix) const
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
        int pix_x = _rszs.image_width_ - 32;
        if (has_application_flag(flags_, ApplicationFlagE::Aquired) || has_application_flag(flags_, ApplicationFlagE::Default)) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _acquired_pix);
            pix_x -= 32;
        }
        if (has_application_flag(flags_, ApplicationFlagE::Owned)) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _owned_pix);
            pix_x -= 32;
        }
        if (has_application_flag(flags_, ApplicationFlagE::ReviewRequest)) {
            painter->drawPixmap(QRect(pix_x, 0, 32, 32), _review_pix);
            pix_x -= 32;
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

ListModel::ListModel(Engine& _rengine, const Sizes& _rsizes, QObject* parent)
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
    const std::string&  _build_id,
    const vector<char>& _image,
    const uint32_t      _flags)
{
    //called on pool thread
    ListItem item;
    item.engine_index_ = _index;
    item.brief_        = QString::fromStdString(_brief);
    item.company_      = QString::fromStdString(_company);
    item.name_         = QString::fromStdString(_name);
    item.build_id_     = QString::fromStdString(_build_id);
    item.flags_        = _flags;
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
    , review_pix_(":/images/review.png")
{
}

void ItemDelegate::paint(QPainter* painter, const QStyleOptionViewItem& option,
    const QModelIndex& index) const
{
    if (index.data().canConvert<const ListItem*>()) {
        const ListItem* pitem = qvariant_cast<const ListItem*>(index.data());

        painter->save();

        pitem->paint(painter, rsizes_, option, acquired_pix_, owned_pix_, review_pix_);

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
    qRegisterMetaType<std::shared_ptr<myapps::front::main::FetchBuildConfigurationResponse>>("std::shared_ptr<myapps::front::FetchBuildConfigurationResponse>");
    qRegisterMetaType<std::shared_ptr<myapps::front::main::FetchAppResponse>>("std::shared_ptr<myapps::front::FetchAppResponse>");

    setWindowFlags(windowFlags() & (~Qt::WindowMaximizeButtonHint));

    pimpl_->store_form_.setupUi(this);
    pimpl_->list_form_.setupUi(pimpl_->store_form_.listWidget);
    pimpl_->item_form_.setupUi(pimpl_->store_form_.itemWidget);
    pimpl_->about_form_.setupUi(pimpl_->store_form_.aboutWidget);
    pimpl_->configure_form_.setupUi(pimpl_->store_form_.configureWidget);

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
    pimpl_->list_form_.listView->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    pimpl_->list_form_.listView->verticalScrollBar()->setSingleStep(20 * pimpl_->scale_y_);
    
    pimpl_->about_form_.image_label->setPixmap(QPixmap(":/images/store_bag.png"));

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

    connect(this, SIGNAL(itemData(int, std::shared_ptr<myapps::front::FetchBuildConfigurationResponse>)), this, SLOT(itemDataSlot(int, std::shared_ptr<myapps::front::FetchBuildConfigurationResponse>)), Qt::QueuedConnection);

    connect(this, SIGNAL(itemEntries(int, std::shared_ptr<myapps::front::FetchAppResponse>)), this, SLOT(itemEntriesSlot(int, std::shared_ptr<myapps::front::FetchAppResponse>)), Qt::QueuedConnection);

    connect(this, SIGNAL(itemAcquire(int, bool)), this, SLOT(itemAcquireSlot(int, bool)), Qt::QueuedConnection);

    connect(pimpl_->item_form_.media_list_widget, SIGNAL(itemDoubleClicked(QListWidgetItem*)), this, SLOT(imageDoubleClicked(QListWidgetItem*)));

    connect(&pimpl_->home_action_, &QAction::triggered, this, &MainWindow::goHomeSlot);
    connect(&pimpl_->account_action_, &QAction::triggered, this, &MainWindow::goAccountSlot);
    connect(&pimpl_->back_action_, &QAction::triggered, this, &MainWindow::goBackSlot);
    connect(&pimpl_->about_action_, &QAction::triggered, this, &MainWindow::goAboutSlot);
    connect(pimpl_->item_form_.configure_button, SIGNAL(clicked(bool)), this, SLOT(onConfigureButtonClicked(bool)));
    connect(pimpl_->item_form_.review_accept_button, SIGNAL(clicked(bool)), this, SLOT(onReviewAcceptButtonClicked(bool)));
    connect(pimpl_->item_form_.review_reject_button, SIGNAL(clicked(bool)), this, SLOT(onReviewRejectButtonClicked(bool)));

    connect(pimpl_->configure_form_.treeWidget, SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this, SLOT(configureItemChangedSlot(QTreeWidgetItem *, QTreeWidgetItem *)));
    
    connect(pimpl_->item_form_.comboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::buildChangedSlot);

    connect(pimpl_->configure_form_.stateComboBox, QOverload<int>::of(&QComboBox::currentIndexChanged), this, &MainWindow::configureStateChangedSlot);

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
    //pimpl_->tool_bar_.addSeparator();
    //pimpl_->tool_bar_.addWidget(ptoolbutton);
    pimpl_->tool_bar_.addSeparator();
    pimpl_->tool_bar_.addAction(&pimpl_->about_action_);

    pimpl_->config_menu_.addAction(&pimpl_->account_action_);

    pimpl_->showWidget(pimpl_->store_form_.listWidget);

    resize(QSize((pimpl_->sizes_.item_width_ * item_column_count + 60), (pimpl_->sizes_.item_height_ * item_row_count + 133)));

    pimpl_->item_form_.review_accept_button->setIcon(QIcon(":/images/review_accept.png"));
    pimpl_->item_form_.review_reject_button->setIcon(QIcon(":/images/review_reject.png"));
    pimpl_->item_form_.configure_button->setIcon(QIcon(":/images/configure.png"));

    pimpl_->item_form_.comboBox->setPlaceholderText("Acquire");

    pimpl_->item_form_.comboBox->clear();

    //pimpl_->item_form_.comboBox->setMaximumHeight(pimpl_->item_form_.review_accept_button->height() - 2);
    pimpl_->item_form_.review_accept_button->setMaximumHeight(pimpl_->item_form_.comboBox->height() + 6);
    pimpl_->item_form_.review_reject_button->setMaximumHeight(pimpl_->item_form_.comboBox->height() + 6);
    pimpl_->item_form_.configure_button->setMaximumHeight(pimpl_->item_form_.comboBox->height() + 6);
    pimpl_->item_form_.acquire_button->setStyleSheet("border: 0px");

    pimpl_->history_.emplace(
        pimpl_->store_form_.listWidget,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.listWidget);
        });

    pimpl_->about_form_.tabWidget->setCurrentIndex(0);
    pimpl_->about_form_.label_title->setText("MyApps.space Store");
    {
        using namespace std;
        ostringstream oss;

        oss << utility::VERSION_MAJOR << '.' << utility::VERSION_MINOR;
        //oss << " - " << client::utility::version_vcs_branch();
        oss << " - <a href=https://github.com/vipalade/ola-client-store/tree/" << utility::version_vcs_commit() << ">" << utility::version_vcs_commit() << "</a>";

        pimpl_->about_form_.label_version->setText(QString::fromStdString(oss.str()));
    }
    {
        using namespace std;
        using namespace std::chrono;
        auto now_c = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
        std::tm ptm;
        localtime_s(&ptm, &now_c);

        {
            ostringstream oss;

            oss << "Copyright 2019-" << std::put_time(&ptm, "%Y") << " MyApps Co. All rights reserved." << endl;
            oss << endl;
            oss << "The program is provided AS IS with NO WARRANTY OF ANY KIND, INCLUDING THE WARRANTY OF DESIGN, MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE." << endl;

            pimpl_->about_form_.label_about->setText(QString::fromStdString(oss.str()));
        }
        {
            ostringstream oss;
            oss << "Open Source Software:" << endl;
            oss << endl;
            oss << " <a href=https://github.com/solidoss/solidframe>SolidFrame</a> - " << solid::VERSION_MAJOR << '.' << solid::VERSION_MINOR;
            oss << " - <a href=https://github.com/solidoss/solidframe/tree/" << solid::version_vcs_commit() << ">" << solid::version_vcs_commit() << "</a>" << endl;
            oss << endl;
            oss << " <a href=https://www.qt.io>Qt</a>" << endl;
            oss << endl;
            oss << " <a href=https://www.boost.org>Boost</a>" << endl;
            oss << endl;
            oss << " <a href=https://www.openssl.org>OpenSSL</a>" << endl;
            oss << endl;
            oss << " <a href=https://github.com/USCiLab/cereal>Cereal</a>" << endl;
            oss << endl;
            oss << " <a href=https://github.com/google/snappy>Snappy</a>" << endl;
            oss << endl;
            oss << " <a href=https://zlib.net>zlib</a>" << endl;
            oss << endl;
            oss << " <a href=https://libzip.org>libzip</a>" << endl;
            oss << endl;
            oss << " <a href=https://github.com/jbeder/yaml-cpp>yaml</a>" << endl;
            oss << endl;

            pimpl_->about_form_.label_credits->setText(QString::fromStdString(oss.str()));
        }
        {
            ostringstream oss;
            oss << "MyApps.space Store, uses the LGPL, unchanged version of <a href=https://www.qt.io>Qt</a>"<<endl<<endl;
            oss << "It uses the Qt libraries installed by the related open source project - <a href=https://github.com/vipalade/ola-client>MyApps.store Client</a>"<<endl<<endl;
            oss << "In order to use MyApps.space Store application with your own Qt libraries, please follow the below steps:" << endl;
            oss << " * Step 1" << endl;
            oss << " * Step 2" << endl;
            oss << " * Step 3" << endl;
            oss << endl;
            oss << "You can find a copy of the Qt source code here" << endl;

            pimpl_->about_form_.label_qt->setText(QString::fromStdString(oss.str()));
        }
    }

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

    if (pimpl_->current_item_ == _index) return;

    pimpl_->current_item_ = _index;
    pimpl_->config_current_build_.clear();
    pimpl_->config_current_media_.clear();

    auto& item            = pimpl_->list_model_.item(pimpl_->current_item_);
    pimpl_->item_form_.frame->setFixedHeight(pimpl_->sizes_.image_height_);
    pimpl_->item_form_.image_label->setFixedSize(img_size);
    pimpl_->item_form_.image_label->setPixmap(QPixmap::fromImage(item.image_));
    pimpl_->item_form_.name_label->setText(item.name_);
    pimpl_->item_form_.company_label->setText(item.company_);
    pimpl_->item_form_.brief_label->setText(item.brief_);
    pimpl_->item_form_.description_label->setText("");
    pimpl_->item_form_.release_label->setText("");

    if (item.data_ptr_) {
        pimpl_->item_form_.description_label->setText(QString::fromStdString(item.data_ptr_->configuration_.property_vec_[3].second));
        pimpl_->item_form_.release_label->setText(QString::fromStdString(item.data_ptr_->configuration_.property_vec_[4].second));
    }

    pimpl_->item_form_.media_list_widget->hide();
    pimpl_->item_form_.media_list_widget->clear();

    pimpl_->item_form_.acquire_button->setChecked(has_application_flag(item.flags_, ApplicationFlagE::Aquired));
    pimpl_->item_form_.acquire_button->setEnabled(!has_application_flag(item.flags_, ApplicationFlagE::Default));

    bool enable_combo = false;

    if (has_application_flag(item.flags_, ApplicationFlagE::Aquired) || has_application_flag(item.flags_, ApplicationFlagE::Default)) {
        enable_combo = true;
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquire_on.png"));
    }
    else {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquire_off.png"));
    }

#if 0
    if (item.acquired_ && item.owned_) {
        enable_combo = true;
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired_owned.png"));
    } else if (item.acquired_ || item.default_) {
        enable_combo = true;
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired.png"));
    } else if (item.owned_) {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/owned.png"));
    } else {
        pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/empty.png"));
    }
#endif

    
    pimpl_->item_form_.review_accept_button->setEnabled(enable_combo);
    pimpl_->item_form_.review_reject_button->setEnabled(enable_combo);
    if (has_application_flag(item.flags_, ApplicationFlagE::Owned)) {
        pimpl_->item_form_.configure_button->show();
    }
    else {
        pimpl_->item_form_.configure_button->hide();
    }
    pimpl_->item_form_.comboBox->setEnabled(enable_combo);

    pimpl_->engine().fetchItemEntries(
        item.engine_index_,
        [this, index = _index](std::shared_ptr<myapps::front::main::FetchAppResponse>& _response_ptr) {
            //called on another thread - need to move the data onto GUI thread
            emit itemEntries(index, _response_ptr);
        });
}

void MainWindow::itemDataSlot(int _index, std::shared_ptr<myapps::front::main::FetchBuildConfigurationResponse> _response_ptr)
{
    if (_response_ptr && _response_ptr->error_ == 0) {
        auto& item = pimpl_->list_model_.item(_index);
        item.data_ptr_ = std::move(_response_ptr);

        //TODO: ugly, order for items is set in 
        //store engine and used here
        item.name_ = QString::fromStdString(item.data_ptr_->configuration_.property_vec_[0].second);
        item.company_ = QString::fromStdString(item.data_ptr_->configuration_.property_vec_[1].second);
        item.brief_ = QString::fromStdString(item.data_ptr_->configuration_.property_vec_[2].second);
        pimpl_->item_form_.name_label->setText(item.name_);
        pimpl_->item_form_.company_label->setText(item.company_);
        pimpl_->item_form_.brief_label->setText(item.brief_);
        pimpl_->item_form_.description_label->setText(QString::fromStdString(item.data_ptr_->configuration_.property_vec_[3].second));
        pimpl_->item_form_.release_label->setText(QString::fromStdString(item.data_ptr_->configuration_.property_vec_[4].second));

        QImage img;
        if (img.loadFromData(reinterpret_cast<const uchar*>(item.data_ptr_->image_blob_.data()), item.data_ptr_->image_blob_.size())) {
            item.image_ = img.scaled(QSize(pimpl_->sizes_.image_width_, pimpl_->sizes_.image_height_), Qt::KeepAspectRatio, Qt::SmoothTransformation);
        }

        pimpl_->item_form_.image_label->setPixmap(QPixmap::fromImage(item.image_));

        showMediaThumbnails(_index);
    }
}

const char* build_status_to_image_name(const myapps::utility::AppItemStateE _status)
{
    using AppItemStateE = myapps::utility::AppItemStateE;
    switch (_status) {
    case AppItemStateE::Invalid:
        return ":/images/none.png";
    case AppItemStateE::Trash:
        return ":/images/trash.png";
    case AppItemStateE::PrivateAlpha:
        return ":/images/private_alpha.png";
    case AppItemStateE::ReviewRequest:
        return ":/images/review_requested.png";
    case AppItemStateE::ReviewStarted:
        return ":/images/review_started.png";
    case AppItemStateE::ReviewAccepted:
        return ":/images/review_accepted.png";
    case AppItemStateE::ReviewRejected:
        return ":/images/review_rejected.png";
    case AppItemStateE::PublicAlpha:
        return ":/images/public_alpha.png";
    case AppItemStateE::PublicBeta:
        return ":/images/public_beta.png";
    case AppItemStateE::PublicRelease:
        return ":/images/public_release.png";
    default:
        return "";
    }
}

void MainWindow::itemEntriesSlot(int _index, std::shared_ptr<myapps::front::main::FetchAppResponse> _response_ptr){
    using namespace myapps::utility;

    if (!_response_ptr) return;
    if (_response_ptr->error_ != 0) {
        solid_log(logger, Error, "FetchAppResponse error: "<< _response_ptr->error_<<" message: "<< _response_ptr->message_);
        if (_response_ptr->item_vec_.empty()) {
            return;
        }
    }
    if (_index != pimpl_->current_item_) return;

    sort(
        _response_ptr->item_vec_.begin(),
        _response_ptr->item_vec_.end(),
        [](const AppItemEntry& _e1, const AppItemEntry& _e2) {
            if (_e1.type() < _e2.type()) {
                return true;
            }
            else if (_e1.type() > _e2.type()){
                return false;
            }
            else if (_e1.state() > _e2.state()) {
                return true;
            }
            else if (_e1.state() < _e2.state()) {
                return false;
            }
            else {
                return solid::cstring::cmp(_e1.name_.c_str(), _e2.name_.c_str()) > 0;
            }
        }
    );

    auto& item = pimpl_->list_model_.item(_index);

    pimpl_->item_form_.comboBox->clear();

    pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicRelease)), tr("Latest Available"));
    pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicRelease)), tr("Latest Public Release"));
    pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicBeta)), tr("Latest Public Beta"));
    pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicAlpha)), tr("Latest Public Alpha"));
    pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::Invalid)), tr("Hide"));

    if (has_application_flag(item.flags_, ApplicationFlagE::Owned)) {
        pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PrivateAlpha)), tr("Private Alpha"));
        pimpl_->item_form_.configure_button->show();
    }
    else {
        pimpl_->item_form_.configure_button->hide();
    }

    pimpl_->item_form_.review_accept_button->hide();
    pimpl_->item_form_.review_reject_button->hide();

    bool found_any_build = false;
    for (const auto& e : _response_ptr->item_vec_)
    {
        if (e.type() == AppItemTypeE::Build) {
            if (e.state() != AppItemStateE::Trash) {
                found_any_build = true;
                QString name = QString::fromStdString(e.name_);
                pimpl_->item_form_.comboBox->addItem(QIcon(build_status_to_image_name(e.state())), name, e.value());
            }
        }
    }

    if (found_any_build) {
        int index = -1;
        if (item.build_id_.isEmpty()) {
            index = 0;
        }
        else if (item.build_id_ == app_item_public_release) {
            index = 1;
        }
        else if (item.build_id_ == app_item_public_beta) {
            index = 2;
        }
        else if (item.build_id_ == app_item_public_alpha) {
            index = 3;
        }
        else if (item.build_id_ == app_item_invalid) {
            index = 4;
        }
        else if (has_application_flag(item.flags_, ApplicationFlagE::Owned) && item.build_id_ == myapps::utility::app_item_private_alpha) {
            index = 5;
        }
        else {
            for (int i = 5; i < pimpl_->item_form_.comboBox->count(); ++i) {
                if (item.build_id_ == pimpl_->item_form_.comboBox->itemText(i)) {
                    index = i;
                    AppItemEntry entry{ pimpl_->item_form_.comboBox->itemData(i).toULongLong()};

                    if (!has_application_flag(item.flags_, ApplicationFlagE::Owned) && entry.state() >= AppItemStateE::ReviewRequest && entry.state() <= AppItemStateE::ReviewRejected) {
                        pimpl_->item_form_.review_accept_button->show();
                        pimpl_->item_form_.review_reject_button->show();
                    }
                    break;
                }
            }
            if (index < 0) {
                index = 0;
            }

        }

        pimpl_->item_form_.comboBox->setCurrentIndex(index);
    }

    if (has_application_flag(item.flags_, ApplicationFlagE::Owned)) {
        prepareConfigureForm(_index, _response_ptr);
    }
}

void MainWindow::prepareConfigureForm(int _index, std::shared_ptr<myapps::front::main::FetchAppResponse> _response_ptr)
{
    pimpl_->configure_form_.treeWidget->setColumnCount(1);
    pimpl_->configure_form_.treeWidget->setSelectionMode(QAbstractItemView::SingleSelection);

    //pimpl_->configure_form_.treeWidget->clear();

    QList<QTreeWidgetItem*> build_items;
    QList<QTreeWidgetItem*> media_items;

    if (pimpl_->pcurrent_build_parent_ == nullptr) {
        pimpl_->pcurrent_build_parent_ = new QTreeWidgetItem(static_cast<QTreeWidget*>(nullptr), QStringList{ tr("Builds") });
        pimpl_->pcurrent_media_parent_ = new QTreeWidgetItem(static_cast<QTreeWidget*>(nullptr), QStringList{ tr("Media") });
    }
    else {
        foreach(auto i, pimpl_->pcurrent_build_parent_->takeChildren()) delete i;
        foreach(auto i, pimpl_->pcurrent_media_parent_->takeChildren()) delete i;
    }

    for (const auto& e : _response_ptr->item_vec_)
    {
        QString name = QString::fromStdString(e.name_);
        auto pitem = new QTreeWidgetItem(static_cast<QTreeWidget*>(nullptr), QStringList(name));
        pitem->setIcon(0, QIcon(build_status_to_image_name(e.state())));
        pitem->setData(0, Qt::UserRole, e.value());
        if (e.type() == myapps::utility::AppItemTypeE::Build) {
            build_items.append(pitem);
        }
        else if (e.type() == myapps::utility::AppItemTypeE::Media) {
            media_items.append(pitem);
        }
    }

    pimpl_->pcurrent_build_parent_->addChildren(build_items);
    pimpl_->pcurrent_media_parent_->addChildren(media_items);

    pimpl_->pcurrent_build_parent_->setFlags(pimpl_->pcurrent_build_parent_->flags() & (~Qt::ItemIsSelectable));
    pimpl_->configure_form_.treeWidget->addTopLevelItem(pimpl_->pcurrent_build_parent_);
    pimpl_->pcurrent_media_parent_->setFlags(pimpl_->pcurrent_media_parent_->flags() & (~Qt::ItemIsSelectable));
    pimpl_->configure_form_.treeWidget->addTopLevelItem(pimpl_->pcurrent_media_parent_);

    pimpl_->configure_form_.stateComboBox->hide();


    if (!pimpl_->config_current_build_.isEmpty()) {
        bool found = false;
        foreach(auto i, pimpl_->configure_form_.treeWidget->findItems(pimpl_->config_current_build_, Qt::MatchFixedString | Qt::MatchRecursive)) {
            if (i->parent() == pimpl_->pcurrent_build_parent_) {
                found = true;
                pimpl_->configure_form_.treeWidget->setCurrentItem(i);
                break;
            }
        }

        if (!found) {
            pimpl_->config_current_build_.clear();
        }
    }else if (!pimpl_->config_current_media_.isEmpty()) {
        bool found = false;
        foreach(auto i, pimpl_->configure_form_.treeWidget->findItems(pimpl_->config_current_media_, Qt::MatchFixedString | Qt::MatchRecursive)) {
            if (i->parent() == pimpl_->pcurrent_media_parent_) {
                found = true;
                pimpl_->configure_form_.treeWidget->setCurrentItem(i);
                break;
            }
        }

        if (!found) {
            pimpl_->config_current_build_.clear();
        }
    }

    pimpl_->configure_form_.treeWidget->expandAll();
}

void MainWindow::itemAcquireSlot(int _index, bool _acquired)
{
    auto& item = pimpl_->list_model_.item(_index);


    set_application_flag(item.flags_, ApplicationFlagE::Aquired, _acquired);

    if (_index == pimpl_->current_item_) {
        pimpl_->item_form_.acquire_button->setChecked(_acquired);
        pimpl_->item_form_.acquire_button->setEnabled(!has_application_flag(item.flags_, ApplicationFlagE::Default));
        bool enable_combo = false;
        if (has_application_flag(item.flags_, ApplicationFlagE::Aquired) || has_application_flag(item.flags_, ApplicationFlagE::Default)) {
            enable_combo = true;
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquire_on.png"));
        }
        else {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquire_off.png"));
        }
#if 0
        if (item.acquired_ && item.owned_) {
            enable_combo = true;
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired_owned.png"));
        } else if (item.acquired_ || item.default_) {
            enable_combo = true;
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/acquired.png"));
        } else if (item.owned_) {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/owned.png"));
        } else {
            pimpl_->item_form_.acquire_button->setIcon(QIcon(":/images/empty.png"));
        }
#endif
        pimpl_->item_form_.comboBox->setEnabled(enable_combo);
        pimpl_->item_form_.review_accept_button->setEnabled(enable_combo);
        pimpl_->item_form_.review_reject_button->setEnabled(enable_combo);
        pimpl_->item_form_.comboBox->setCurrentIndex(0);
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

    if (item.data_ptr_ && item.data_ptr_->configuration_.media_.entry_vec_.size()) {
        const QSize thumb_size{pimpl_->sizes_.image_width_, pimpl_->sizes_.image_height_};
        pimpl_->item_form_.media_list_widget->clear();
        pimpl_->item_form_.media_list_widget->setIconSize(thumb_size);
        pimpl_->item_form_.media_list_widget->setGridSize(thumb_size);
        pimpl_->item_form_.media_list_widget->setMinimumHeight(pimpl_->sizes_.image_height_ + 20);
        bool   has_image = false;
        size_t index     = 0;
        for (const auto& entry : item.data_ptr_->configuration_.media_.entry_vec_) {
            QImage image;
            if (image.load(QString::fromStdString(entry.thumbnail_path_))) {
                auto* pitem = new ThumbnailItem(index);
                pitem->setData(Qt::DecorationRole, QPixmap::fromImage(image.scaled(thumb_size, Qt::KeepAspectRatio, Qt::SmoothTransformation)));
                pitem->setSizeHint(thumb_size + QSize(4, 4));
                pimpl_->item_form_.media_list_widget->addItem(pitem);
                has_image = true;
            } else {
                solid_log(logger, Warning, "Failed loading media: " << entry.thumbnail_path_);
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

    if (!has_application_flag(item.flags_, ApplicationFlagE::Default)) {
        pimpl_->engine().acquireItem(
            item.engine_index_,
            _checked,
            [this, index = pimpl_->current_item_](bool _acquired) {
            emit itemAcquire(index, _acquired);
        });
    }
}

void MainWindow::onConfigureButtonClicked(bool _checked) {
    pimpl_->historyPush(
        nullptr,
        [this]() {
            pimpl_->showWidget(pimpl_->store_form_.configureWidget);
    })();
}

void MainWindow::imageDoubleClicked(QListWidgetItem* _item)
{
    auto* pthumb = static_cast<ThumbnailItem*>(_item);

    pimpl_->historyPush(
        nullptr,
        [this, item_index = pimpl_->current_item_, image_index = pthumb->index_]() {
            const auto&    item = pimpl_->list_model_.item(item_index);
            const QString  path = QString::fromStdString(item.data_ptr_->configuration_.media_.entry_vec_[image_index].path_);

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

void MainWindow::buildChangedSlot(int _index)
{
    using namespace myapps::utility;

    if (pimpl_->item_form_.comboBox->count() == 0) {
        return;
    }

    auto& item = pimpl_->list_model_.item(pimpl_->current_item_);
    QString build_id;
    if (_index == 0) {
    }
    else if (_index == 1) {
        build_id = app_item_public_release;
    }
    else if (_index == 2) {
        build_id = app_item_public_beta;
    }
    else if (_index == 3) {
        build_id = app_item_public_alpha;
    }
    else if (_index == 4) {
        build_id = app_item_invalid;
    }
    else if (has_application_flag(item.flags_, ApplicationFlagE::Owned) && _index == 5) {
        build_id = app_item_private_alpha;
    }
    else {
        build_id = pimpl_->item_form_.comboBox->itemText(_index);
    }
    
    if (build_id != item.build_id_) {
        AppItemEntry entry{ pimpl_->item_form_.comboBox->itemData(_index).toULongLong() };

        if (!has_application_flag(item.flags_, ApplicationFlagE::Owned) && entry.state() >= AppItemStateE::ReviewRequest && entry.state() <= AppItemStateE::ReviewRejected) {
            pimpl_->item_form_.review_accept_button->show();
            pimpl_->item_form_.review_reject_button->show();

            if (entry.state() == AppItemStateE::ReviewRequest) {
                entry.name_ = build_id.toStdString();

                pimpl_->engine().changeAppItemState(
                    item.engine_index_,
                    entry, static_cast<int>(AppItemStateE::ReviewStarted),
                    [this, index = pimpl_->current_item_, engine_index = item.engine_index_](std::shared_ptr<myapps::front::core::Response>& _response_ptr) {
                    solid_log(logger, Verbose, "ChangeAppItemState response: " << _response_ptr->error_ << " message: " << _response_ptr->message_);

                    pimpl_->engine().fetchItemEntries(
                        engine_index,
                        [this, index](std::shared_ptr<myapps::front::main::FetchAppResponse>& _response_ptr) {
                            //called on another thread - need to move the data onto GUI thread
                            emit itemEntries(index, _response_ptr);
                        });
                }
                );
            }
        }
        else {
            pimpl_->item_form_.review_accept_button->hide();
            pimpl_->item_form_.review_reject_button->hide();
        }

        item.build_id_ = build_id;
        item.data_ptr_.reset();
        pimpl_->engine().acquireBuild(item.engine_index_, item.build_id_.toStdString());
    }

    if (!item.data_ptr_) {
        string build_id = item.build_id_.toStdString();
        if (build_id == myapps::utility::app_item_invalid) {
            build_id.clear();
        }
        pimpl_->engine().fetchItemData(
            item.engine_index_,
            build_id,
            [this, index = pimpl_->current_item_](std::shared_ptr<myapps::front::main::FetchBuildConfigurationResponse>& _response_ptr) {
            //called on another thread - need to move the data onto GUI thread
            emit itemData(index, _response_ptr);
        });
    }
    else {
        showMediaThumbnails(pimpl_->current_item_);
    }
}

void MainWindow::configureItemChangedSlot(QTreeWidgetItem* _pcurrent, QTreeWidgetItem* _pprevious)
{
    if (_pcurrent == nullptr) return;

    if (_pcurrent->parent() == pimpl_->pcurrent_build_parent_) {
        pimpl_->config_current_build_ = _pcurrent->text(0);
        pimpl_->configure_form_.stateComboBox->show();
        
        const myapps::utility::AppItemEntry entry{ _pcurrent->data(0, Qt::UserRole).toULongLong() };

        pimpl_->configureBuildPrepareStateComboBox(entry);
    }
    else if (_pcurrent->parent() == pimpl_->pcurrent_media_parent_)
    {
        pimpl_->config_current_media_ = _pcurrent->text(0);
        pimpl_->configure_form_.stateComboBox->show();

        const myapps::utility::AppItemEntry entry{ _pcurrent->data(0, Qt::UserRole).toULongLong() };

        pimpl_->configureMediaPrepareStateComboBox(entry);
    }
    else {
        pimpl_->configure_form_.stateComboBox->hide();
    }

    pimpl_->configure_form_.frame->update();
}

void MainWindow::Data::configureMediaPrepareStateComboBox(const myapps::utility::AppItemEntry& _item) {
    using namespace myapps::utility;
    configure_form_.stateComboBox->clear();
    configure_form_.stateComboBox->setPlaceholderText(tr("Invalid"));
    configure_form_.stateComboBox->setCurrentIndex(-1);

    configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::Trash)), tr("Trash"), static_cast<int>(AppItemStateE::Trash));
    configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicRelease)), tr("Public Release"), static_cast<int>(AppItemStateE::PublicRelease));

    switch (_item.state()) {
    case AppItemStateE::Invalid:break;
    case AppItemStateE::Trash:
        configure_form_.stateComboBox->setCurrentIndex(0);
        break;
    case AppItemStateE::PublicRelease:
        configure_form_.stateComboBox->setCurrentIndex(1);
        break;
    default:
        break;
    }
}

void MainWindow::Data::configureBuildPrepareStateComboBox(const myapps::utility::AppItemEntry& _item) {
    using namespace myapps::utility;
    configure_form_.stateComboBox->clear();
    configure_form_.stateComboBox->setPlaceholderText(tr("Invalid"));
    configure_form_.stateComboBox->setCurrentIndex(-1);

    if (_item.isFlagSet(AppItemFlagE::ReviewAccepted)) {
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::Trash)), tr("Trash"), static_cast<int>(AppItemStateE::Trash));
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PrivateAlpha)), tr("Private Alpha"), static_cast<int>(AppItemStateE::PrivateAlpha));
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicAlpha)), tr("Public Alpha"), static_cast<int>(AppItemStateE::PublicAlpha));
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicBeta)), tr("Public Beta"), static_cast<int>(AppItemStateE::PublicBeta));
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PublicRelease)), tr("Public Release"), static_cast<int>(AppItemStateE::PublicRelease));
    }
    else if(_item.isFlagSet(AppItemFlagE::ReviewRejected)) {
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::Trash)), tr("Trash"), static_cast<int>(AppItemStateE::Trash));
        configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PrivateAlpha)), tr("Private Alpha"), static_cast<int>(AppItemStateE::PrivateAlpha));
    }
    else {
        if (_item.state() != AppItemStateE::ReviewStarted) {
            configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::Trash)), tr("Trash"), static_cast<int>(AppItemStateE::Trash));
            configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::PrivateAlpha)), tr("Private Alpha"), static_cast<int>(AppItemStateE::PrivateAlpha));
            configure_form_.stateComboBox->addItem(QIcon(build_status_to_image_name(AppItemStateE::ReviewRequest)), tr("Review Request"), static_cast<int>(AppItemStateE::ReviewRequest));
        }
    }

    switch (_item.state()) {
    case AppItemStateE::Invalid:break;
    case AppItemStateE::Trash:
        configure_form_.stateComboBox->setCurrentIndex(0);
        break;
    case AppItemStateE::PrivateAlpha:
        configure_form_.stateComboBox->setCurrentIndex(1);
        break;
    case AppItemStateE::ReviewRequest:
        configure_form_.stateComboBox->setCurrentIndex(2);
        break;
    case AppItemStateE::ReviewStarted:
        configure_form_.stateComboBox->setPlaceholderText(tr("Review Started"));
        configure_form_.stateComboBox->setCurrentIndex(-1);
        break;
    case AppItemStateE::ReviewAccepted:
        configure_form_.stateComboBox->setPlaceholderText(tr("Review Accepted"));
        configure_form_.stateComboBox->setCurrentIndex(-1);
        break;
    case AppItemStateE::ReviewRejected:
        configure_form_.stateComboBox->setPlaceholderText(tr("Review Rejected"));
        configure_form_.stateComboBox->setCurrentIndex(-1);
        break;
    case AppItemStateE::PublicAlpha:
        configure_form_.stateComboBox->setCurrentIndex(2);
        break;
    case AppItemStateE::PublicBeta:
        configure_form_.stateComboBox->setCurrentIndex(3);
        break;
    case AppItemStateE::PublicRelease:
        configure_form_.stateComboBox->setCurrentIndex(4);
        break;
    default:
        break;
    }
}

void MainWindow::configureStateChangedSlot(int _index) {
    using namespace myapps::utility;

    auto pcurrent_item = pimpl_->configure_form_.treeWidget->currentItem();

    if (pimpl_->configure_form_.stateComboBox->count() == 0) {
        return;
    }

    AppItemEntry item_entry{ pcurrent_item->data(0, Qt::UserRole).toULongLong() };
    const int req_state = pimpl_->configure_form_.stateComboBox->itemData(_index).toInt();
    if (static_cast<int>(item_entry.state()) != req_state)
    {
        //item_entry.state(static_cast<AppItemStateE>(req_state));
        //pcurrent_item->setData(0, Qt::UserRole, item_entry.value());
        auto& item = pimpl_->list_model_.item(pimpl_->current_item_);

        item_entry.name_ = pcurrent_item->text(0).toStdString();
        pimpl_->engine().changeAppItemState(
            item.engine_index_,
            item_entry, req_state,
            [this, index = pimpl_->current_item_, engine_index = item.engine_index_](std::shared_ptr<myapps::front::core::Response>& _response_ptr) {
                solid_log(logger, Verbose, "ChangeAppItemState response: "<<_response_ptr->error_<<" message: "<<_response_ptr->message_);
                
                pimpl_->engine().fetchItemEntries(
                    engine_index,
                    [this, index](std::shared_ptr<myapps::front::main::FetchAppResponse>& _response_ptr) {
                        //called on another thread - need to move the data onto GUI thread
                        emit itemEntries(index, _response_ptr);
                    });
            }
        );
    }
}

void MainWindow::onReviewAcceptButtonClicked(bool _checked) {
    using namespace myapps::utility;
    auto index = pimpl_->item_form_.comboBox->currentIndex();
    AppItemEntry entry{ pimpl_->item_form_.comboBox->itemData(index).toULongLong() };
    
    entry.name_ = pimpl_->item_form_.comboBox->itemText(index).toStdString();
    auto& item = pimpl_->list_model_.item(pimpl_->current_item_);

    pimpl_->engine().changeAppItemState(
        item.engine_index_,
        entry, static_cast<int>(AppItemStateE::ReviewAccepted),
        [this, index = pimpl_->current_item_, engine_index = item.engine_index_](std::shared_ptr<myapps::front::core::Response>& _response_ptr) {
        solid_log(logger, Verbose, "ChangeAppItemState response: " << _response_ptr->error_ << " message: " << _response_ptr->message_);

        pimpl_->engine().fetchItemEntries(
            engine_index,
            [this, index](std::shared_ptr<myapps::front::main::FetchAppResponse>& _response_ptr) {
                //called on another thread - need to move the data onto GUI thread
                emit itemEntries(index, _response_ptr);
            });
    }
    );
}

void MainWindow::onReviewRejectButtonClicked(bool _checked) {
    using namespace myapps::utility;
    auto index = pimpl_->item_form_.comboBox->currentIndex();
    AppItemEntry entry{ pimpl_->item_form_.comboBox->itemData(index).toULongLong() };

    entry.name_ = pimpl_->item_form_.comboBox->itemText(index).toStdString();
    auto& item = pimpl_->list_model_.item(pimpl_->current_item_);

    pimpl_->engine().changeAppItemState(
        item.engine_index_,
        entry, static_cast<int>(AppItemStateE::ReviewRejected),
        [this, index = pimpl_->current_item_, engine_index = item.engine_index_](std::shared_ptr<myapps::front::core::Response>& _response_ptr) {
        solid_log(logger, Verbose, "ChangeAppItemState response: " << _response_ptr->error_ << " message: " << _response_ptr->message_);

        pimpl_->engine().fetchItemEntries(
            engine_index,
            [this, index](std::shared_ptr<myapps::front::main::FetchAppResponse>& _response_ptr) {
                //called on another thread - need to move the data onto GUI thread
                emit itemEntries(index, _response_ptr);
            });
    }
    );
}

} //namespace store
} //namespace client
} //namespace myapps
