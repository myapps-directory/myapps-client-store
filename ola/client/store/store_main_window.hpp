#pragma once

#include "solid/system/pimpl.hpp"
#include "store_engine.hpp"
#include <QAbstractListModel>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <functional>
#include <string>
#include <deque>
#include <mutex>

namespace ola {
namespace client {
namespace store {

struct ListItem {
    QString name_;
    QString company_;
    QString brief_;
    QImage  image_;

    void paint(QPainter* painter, const QStyleOptionViewItem& option)const;
};

Q_DECLARE_METATYPE(const ListItem*)

class ListModel : public QAbstractListModel {
    Q_OBJECT
    Engine&                                 rengine_;
    std::deque<ListItem> item_dq_;
    int                  count_ = 0;
    std::mutex           mutex_;
    std::deque<std::pair<size_t, ListItem>> push_item_dq_;
    std::deque<std::pair<size_t, ListItem>> pop_item_dq_;
    size_t                                  last_fetch_count_ = 0;
    mutable bool                            requested_more_   = false;
    size_t                                  request_more_index_ = 0;

public:
    ListModel(Engine &_rengine, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void prepareAndPushItem(
        const size_t _index,
        const size_t _fetch_count,
        const std::string&  _name,
        const std::string&  _company,
        const std::string&  _brief,
        const std::vector<char>& _image);
signals:
    void newItemSignal();
    void numberPopulated(int number);

private slots:
    void newItemsSlot();
};

class ItemDelegate : public QStyledItemDelegate {
    Q_OBJECT
public:
    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
         const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex&                     index) const override;
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
