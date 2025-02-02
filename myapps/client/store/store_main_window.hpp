// myapps/client/store/store_main_window.hpp

// This file is part of MyApps.directory project
// Copyright (C) 2020, 2021, 2022, 2023, 2024, 2025 Valentin Palade (vipalade @ gmail . com)

// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// at your option any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <https://www.gnu.org/licenses/>.

#pragma once

#include "solid/system/pimpl.hpp"
#include "store_engine.hpp"
#include <QAbstractListModel>
#include <QListWidgetItem>
#include <QMainWindow>
#include <QStyledItemDelegate>
#include <QTreeWidgetItem>
#include <deque>
#include <functional>
#include <mutex>
#include <string>

namespace myapps {
namespace client {
namespace store {

struct Sizes {
    double scale_x_;
    double scale_y_;
    int    image_width_;
    int    image_height_;
    int    item_width_;
    int    item_height_;

    Sizes(
        double _scale_x,
        double _scale_y,
        int    _image_width,
        int    _image_height,
        int    _item_width,
        int    _item_height)
        : scale_x_(_scale_x)
        , scale_y_(_scale_y)
        , image_width_(_image_width * scale_x_)
        , image_height_(_image_height * scale_y_)
        , item_width_(_item_width * scale_x_)
        , item_height_(_item_height * scale_y_)
    {
    }
};

struct ListItem {
    size_t   engine_index_ = -1;
    QString  name_;
    QString  company_;
    QString  brief_;
    QImage   image_;
    QString  build_id_;
    uint32_t flags_;

    solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse> data_ptr_;

    void paint(
        QPainter* painter, const Sizes& _rszs,
        const QStyleOptionViewItem& option,
        const QPixmap& _acquired_pix, const QPixmap& _owned_pix, const QPixmap& _review_pix) const;
};

Q_DECLARE_METATYPE(const ListItem*)
Q_DECLARE_METATYPE(solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse>)
Q_DECLARE_METATYPE(solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchAppResponse>)

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
    const Sizes&         rsizes_;

public:
    ListModel(Engine& _rengine, const Sizes& _rsizes, QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = QModelIndex()) const override;

    QVariant data(const QModelIndex& index, int role = Qt::DisplayRole) const override;

    Qt::ItemFlags flags(const QModelIndex& index) const override;

    void prepareAndPushItem(
        const size_t             _index,
        const size_t             _count,
        const std::string&       _name,
        const std::string&       _company,
        const std::string&       _brief,
        const std::string&       _build_id,
        const std::vector<char>& _image,
        const uint32_t           _flags = 0);
    void prepareAndPushItem(
        const size_t _index,
        const size_t _count);

    const ListItem& item(const size_t _index) const
    {
        return item_dq_[_index];
    }

    ListItem& item(const size_t _index)
    {
        return item_dq_[_index];
    }

    Engine& engine()
    {
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
    ItemDelegate(const Sizes& _rszs);
    void  paint(QPainter* painter, const QStyleOptionViewItem& option,
         const QModelIndex& index) const override;
    QSize sizeHint(const QStyleOptionViewItem& option,
        const QModelIndex&                     index) const override;

private:
    const Sizes& rsizes_;
    QPixmap      acquired_pix_;
    QPixmap      owned_pix_;
    QPixmap      review_pix_;
};

class MainWindow : public QMainWindow {
    Q_OBJECT
    using VectorPairStringT = QVector<QPair<QString, QString>>;

public:
    MainWindow(Engine& _rengine, QWidget* parent = 0);
    ~MainWindow();
    ListModel& model();

private:
    void resizeEvent(QResizeEvent* event) override;
signals:
    void closeSignal();
    void onlineSignal(bool);
    void itemData(int _index, solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse> _response_ptr);
    void itemEntries(int _index, solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchAppResponse> _response_ptr);
    void itemAcquire(int _index, bool _acquired);

private slots:
    void onOnline(bool);
    void onItemDoubleClicked(const QModelIndex&);
    void onAquireButtonToggled(bool _checked);
    void onConfigureButtonClicked(bool _checked);
    void onReviewAcceptButtonClicked(bool _checked);
    void onReviewRejectButtonClicked(bool _checked);
    void itemDataSlot(int _index, solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchBuildConfigurationResponse> _response_ptr);
    void itemEntriesSlot(int _index, solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchAppResponse> _response_ptr);
    void itemAcquireSlot(int _index, bool _acquired);
    void imageDoubleClicked(QListWidgetItem*);

    void goHomeSlot(bool);
    void goAccountSlot(bool);
    void goBackSlot(bool);
    void goAboutSlot(bool);

    void buildChangedSlot(int _index);

    void configureItemChangedSlot(QTreeWidgetItem* current, QTreeWidgetItem* previous);

    void configureStateChangedSlot(int _index);
    void onColorSchemeChanged(Qt::ColorScheme scheme);

private:
    void closeEvent(QCloseEvent*) override;
    bool eventFilter(QObject* obj, QEvent* event) override;
    void showMediaThumbnails(int _index);
    void showItem(int _index);

    void prepareConfigureForm(int _index, solid::frame::mprpc::MessagePointerT<myapps::front::main::FetchAppResponse> _response_ptr);

private:
    struct Data;
    solid::PimplT<Data> pimpl_;
};
} // namespace store
} // namespace client
} // namespace myapps
