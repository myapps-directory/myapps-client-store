#pragma once

#include "solid/system/pimpl.hpp"
#include <QAbstractListModel>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <functional>
#include <string>

namespace ola {
namespace client {
namespace store {

class ListModel : public QAbstractListModel {
    Q_OBJECT
public:
    ListModel(QObject* parent = nullptr)
        : QAbstractListModel(parent)
    {
    }
    int rowCount(const QModelIndex& parent = QModelIndex()) const override
    {
        return 1000;
    }

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override
    {
        if (role == Qt::DisplayRole) {
            return QString("Item %1").arg(index.row() + 1);
        }
        return QVariant();
    }

    Qt::ItemFlags flags(const QModelIndex& index) const override {
        if (!index.isValid())
            return Qt::ItemIsEnabled;

        return QAbstractListModel::flags(index) | Qt::ItemIsSelectable;
    }
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
} // namespace store
} //namespace client
} //namespace ola
