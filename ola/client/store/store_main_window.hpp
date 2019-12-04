#pragma once

#include "solid/system/pimpl.hpp"
#include "store_engine.hpp"
#include <QAbstractListModel>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace ola {
namespace client {
namespace store {

struct ListItem {
    size_t  engine_index_ = -1;
    QString name_;
    QString company_;
    QString brief_;
    QImage  image_;
    bool    aquired_ = false;
    bool    owned_   = false;

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QPixmap& _aquired_pix, const QPixmap& _owned_pix) const;
};

Q_DECLARE_METATYPE(const ListItem*)

class ListModel : public QAbstractListModel {
    Q_OBJECT
    Engine&              rengine_;
    std::deque<ListItem> item_dq_;
    int                  count_ = 0;
    std::mutex           mutex_;
    size_t               push_item_count_ = 0;
    std::deque<ListItem> push_item_dq_;
    std::deque<ListItem> pop_item_dq_;
    size_t               fetch_count_        = 10;
    size_t               engine_fetch_index_ = 0;
    mutable bool         requested_more_     = false;
    size_t               request_more_index_ = 0;

public:
    ListModel(Engine& _rengine, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void prepareAndPushItem(
        const size_t             _index,
        const size_t             _count,
        const std::string&       _name,
        const std::string&       _company,
        const std::string&       _brief,
        const std::vector<char>& _image,
        const bool               _aquired = true,
        const bool               _owned   = true);
    void prepareAndPushItem(
        const size_t _index,
        const size_t _count);
    const ListItem& item(const size_t _index) const {
        return item_dq_[_index];
    }
    Engine& engine() {
        return rengine_;
    }
signals:
    void newItemSignal();
    void numberPopulated(int number);

private slots:
    void newItemsSlot();
};

class ItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    ItemDelegate();
    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
         const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex&                     index) const override;

private:
    QPixmap aquired_pix_;
    QPixmap owned_pix_;
};

class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    MainWindow(Engine& _rengine, QWidget* parent = 0);
    ~MainWindow();
    ListModel& model();
signals:
    void closeSignal();
    void offlineSignal(bool);
private slots:
    void onOffline(bool);
    void onItemDoubleClicked(const QModelIndex&);
    void onAquireButtonToggled(bool _checked);
private:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* obj, QEvent* event) override;

private:
    struct Data;
    solid::PimplT<Data> pimpl_;
};
} // namespace store
} //namespace client
} //namespace ola
