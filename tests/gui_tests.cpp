#include <QPlainTextEdit>
#include <QClipboard>
#include "appearance/interface_font.hpp"
#include "editor/editor_window.hpp"
#include "finder/file_drag.hpp"
#include "finder/file_operations.hpp"
#include "finder/finder_service.hpp"
#include "finder/finder_window.hpp"
#include "finder/platinum_style.hpp"
#include "finder/window_surface_id.hpp"
#include <QAbstractButton>
#include <QAction>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QFileIconProvider>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QListWidget>
#include <QToolButton>
#include <QMenuBar>
#include <QHeaderView>
#include <QMimeData>
#include <QTreeWidget>
#include <QLabel>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardItemModel>
#include <QMessageBox>
#include <QPointer>
#include <QPainter>
#include <QProgressDialog>
#include <QPushButton>
#include <QTemporaryDir>
#include <QTextEdit>
#include <QTimer>
#include <QtTest>
#include "finder/file_labels.hpp"
#include "qt_theme_evidence.hpp"

class GuiTests : public QObject
{
    Q_OBJECT
  private slots:
    void finderHeaderMatchesActiveAndInactiveReferenceBand()
    {
        QTemporaryDir root,profile;
        QVERIFY(QDir(root.path()).mkdir("Inside"));
        QPointer<FinderWindow> window=new FinderWindow(root.path(),profile.path(),root.path());
        const auto close=qScopeGuard([&]{if(window)window->close();});
        window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *header=window->findChild<QLabel*>("folderItemCount");QVERIFY(header);
        QCOMPARE(header->height(),21);QCOMPARE(header->alignment(),Qt::AlignCenter);
        QTRY_VERIFY(header->text().startsWith("1 item, "));
        const auto check=[&](QColor field,QColor top,QColor shade,QColor bottom,QColor ink) {
            const auto image=header->grab().toImage();
            if(image.isNull())return false;
            const auto ratio=image.devicePixelRatio();
            const auto pixel=[&](int x,int y){return image.pixelColor(int((x+.5)*ratio),int((y+.5)*ratio));};
            for(int x=0;x<4;++x)for(int y=0;y<21;++y){
                const auto expected=y==0?top:y==19?shade:y==20?bottom:field;
                if(pixel(x,y)!=expected){
                    qWarning() << "Header sample" << x << y << ratio << image.size() << pixel(x,y) << expected;
                    return false;
                }
            }
            bool found=false;
            for(int y=2;y<18;++y)for(int x=4;x<header->width()-4;++x)
                found=found || pixel(x,y)==ink;
            return found;
        };
        QVERIFY(check(QColor("#ddd"),Qt::white,QColor("#aaa"),Qt::black,Qt::black));
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QTRY_VERIFY(check(QColor("#eee"),QColor("#eee"),QColor("#eee"),QColor("#555"),QColor("#777")));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_VERIFY(check(QColor("#ddd"),Qt::white,QColor("#aaa"),Qt::black,Qt::black));
        // The text stays live while inactive, and both modern navigation controls remain.
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QVERIFY(QDir(root.path()).mkdir("Second"));
        QTRY_VERIFY(header->text().startsWith("2 items, "));
        QVERIFY(window->findChild<QLineEdit*>("pathField"));
        QVERIFY(window->findChild<QListWidget*>("placesList"));
    }

    void finderListKeepsMeasuredRowsSortedColumnAndRealMetadata()
    {
        QTemporaryDir root,profile;
        QVERIFY(QDir(root.path()).mkdir("One"));QVERIFY(QDir(root.path()).mkdir("Two"));
        QPointer<FinderWindow> window=new FinderWindow(root.path(),profile.path(),root.path());
        const auto close=qScopeGuard([&]{if(window)window->close();});
        window->setViewMode(true);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *view=window->findChild<FolderList*>();QVERIFY(view);
        auto *model=qobject_cast<QFileSystemModel*>(view->model());QVERIFY(model);
        auto *header=view->header();QCOMPARE(header->height(),21);
        QCOMPARE(header->sectionSize(0),214);QCOMPARE(header->logicalIndex(1),3);
        QCOMPARE(view->iconSize(),QSize(16,16));QCOMPARE(model->columnCount(),4);
        const auto index=model->index(root.path()+"/One");QVERIFY(index.isValid());
        QTRY_COMPARE(view->visualRect(index).height(),19);
        const auto check=[&](int column,QColor field) {
            const auto rect=view->visualRect(index.siblingAtColumn(column));
            const auto shot=view->viewport()->grab().toImage();const auto ratio=shot.devicePixelRatio();
            const auto pixel=[&](int x,int y){return shot.pixelColor(int((x+.5)*ratio),int((y+.5)*ratio));};
            const int x=rect.right()-4;
            return rect.isValid() && pixel(x,rect.top()+1)==field && pixel(x,rect.bottom())==QColor(Qt::white)
                && (column!=0 || (pixel(rect.left()-2,rect.top()+1)==field
                                   && pixel(rect.left()-2,rect.bottom())==QColor(Qt::white)));
        };
        QTRY_VERIFY(check(0,QColor("#ddd")));QTRY_VERIFY(check(3,QColor("#eee")));
        view->sortByColumn(3,Qt::DescendingOrder);
        QTRY_VERIFY(check(0,QColor("#eee")));QTRY_VERIFY(check(3,QColor("#ddd")));
        // Visual column order leaves real model metadata and selection identities intact.
        QVERIFY(!model->data(index.siblingAtColumn(3)).toString().isEmpty());
        view->selectionModel()->select(index,QItemSelectionModel::ClearAndSelect|QItemSelectionModel::Rows);
        QVERIFY(view->selectionModel()->isSelected(index));
        window->setViewMode(false);window->setViewMode(true);
        QCOMPARE(header->logicalIndex(1),3);
    }
    void finderListSelectionPaintsOnlyTheActiveNameAndKeepsRealSelection()
    {
        QTemporaryDir root,profile;
        QVERIFY(QDir(root.path()).mkdir("Selected folder"));
        QPointer<FinderWindow> window=new FinderWindow(root.path(),profile.path(),root.path());
        const auto close=qScopeGuard([&]{if(window)window->close();});
        window->setViewMode(true);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *view=window->findChild<FolderList*>();QVERIFY(view);
        auto *model=qobject_cast<QFileSystemModel*>(view->model());QVERIFY(model);
        const auto index=model->index(root.path()+"/Selected folder");QVERIFY(index.isValid());
        const auto rect=view->visualRect(index);QVERIFY(rect.isValid());
        const auto point=rect.center();view->setFocus();
        QTest::mouseMove(view->viewport(),point);
        view->selectionModel()->clearSelection();
        const auto row=[&](int column) {
            const auto image=view->viewport()->grab().toImage();const auto ratio=image.devicePixelRatio();
            const auto box=view->visualRect(index.siblingAtColumn(column));
            return image.copy(QRect(qRound(box.x()*ratio),qRound(box.y()*ratio),
                qRound(box.width()*ratio),qRound(box.height()*ratio)));
        };
        const auto normal=row(0),metadata=row(3);
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},point);
        QTRY_VERIFY(view->selectionModel()->isSelected(index));
        const auto selected=row(0);QVERIFY(selected!=normal);
        QCOMPARE(row(3),metadata); // Selection never floods metadata columns.
        int changed=0,black=0;
        for(int y=0;y<selected.height();++y)for(int x=0;x<selected.width();++x){
            if(selected.pixelColor(x,y)!=normal.pixelColor(x,y)){
                ++changed;black+=selected.pixelColor(x,y)==QColor(Qt::black);
                QVERIFY(x<selected.width()*3/4); // The spare Name field stays ordinary.
                QVERIFY(y<selected.height()-1); // Keep the measured white row separator.
            }
        }
        QVERIFY(changed>50);QVERIFY(black>50);
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QTRY_COMPARE(row(0),normal);
        QVERIFY(view->selectionModel()->isSelected(index)); // Visual deactivation is not deselection.
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_COMPARE(row(0),selected);
        QCOMPARE(model->filePath(view->selectionModel()->selectedIndexes().first()),root.path()+"/Selected folder");
    }
    void iconViewSelectionUsesMeasuredColorsAndKeepsManualPlacement()
    {
        QStandardItemModel model(1,1);const auto index=model.index(0,0);
        model.setData(index,"Owned icon");
        QPixmap artwork(32,32);artwork.fill(Qt::transparent);
        const QList<QColor> colors{QColor(200,120,80),QColor(160,160,160),QColor(160,160,80),QColor(80,160,240)};
        const QList<QColor> selectedColors{QColor(100,60,40),QColor(80,80,80),QColor(80,80,40),QColor(40,80,120)};
        QPainter painter(&artwork);
        for(int i=0;i<4;++i)painter.fillRect(4+(i%2)*16,4+(i/2)*16,8,8,colors[i]);
        painter.end();model.setData(index,QIcon(artwork),Qt::DecorationRole);
        FolderIcons view;view.resize(320,220);view.setModel(&model);view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));view.activateWindow();QTRY_VERIFY(view.isActiveWindow());
        view.restorePosition(index,{130,80});QTRY_COMPARE(view.visualRect(index).topLeft(),QPoint(130,80));
        const auto rect=view.visualRect(index);const auto point=QPoint(rect.center().x(),rect.top()+16);
        view.setFocus();QTest::mouseMove(view.viewport(),point);view.selectionModel()->clearSelection();
        const auto normal=view.viewport()->grab().toImage();const auto ratio=normal.devicePixelRatio();
        QTest::mouseClick(view.viewport(),Qt::LeftButton,{},point);
        QTRY_VERIFY(view.selectionModel()->isSelected(index));
        const auto selected=view.viewport()->grab().toImage();
        QList<int> found{0,0,0,0};int labelBlack=0;
        for(int y=0;y<normal.height();++y)for(int x=0;x<normal.width();++x){
            const auto color=normal.pixelColor(x,y);
            const int at=y<qRound((rect.top()+34)*ratio) ? colors.indexOf(color) : -1;
            if(at>=0){QCOMPARE(selected.pixelColor(x,y),selectedColors[at]);++found[at];}
            if(y>=qRound((rect.top()+34)*ratio) && color==QColor(Qt::white) && selected.pixelColor(x,y)==QColor(Qt::black))++labelBlack;
        }
        for(const auto count:found)QVERIFY(count>=20);
        QVERIFY(labelBlack>30);
        const QPoint clear(qRound((rect.left()+.5)*ratio),qRound((rect.top()+16.5)*ratio));
        QCOMPARE(normal.pixelColor(clear),QColor(Qt::white));QCOMPARE(selected.pixelColor(clear),QColor(Qt::white));
        QCOMPARE(view.visualRect(index),rect);QCOMPARE(view.gridSize(),QSize(96,78));
        QCOMPARE(view.movement(),QListView::Free);QCOMPARE(view.spacing(),8);QCOMPARE(view.iconSize(),QSize(32,32));
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!view.isActiveWindow());
        QTRY_COMPARE(view.viewport()->grab().toImage(),normal);
        QVERIFY(view.selectionModel()->isSelected(index));
        view.activateWindow();QTRY_VERIFY(view.isActiveWindow());
        QTRY_COMPARE(view.viewport()->grab().toImage(),selected);
        QCOMPARE(view.visualRect(index),rect);QCOMPARE(model.data(index).toString(),QString("Owned icon"));
    }
    void folderSelectionUsesSizeMatchedPrivateArtworkAndKeepsGenericFallbacks()
    {
        QTemporaryDir root;QVERIFY(QDir(root.path()).mkdir("icons"));
        const auto previous=qgetenv("PLATINUM_PERSONAL_ASSETS");
        const auto oldTheme=QIcon::themeName();const auto oldPaths=QIcon::themeSearchPaths();
        const auto restore=qScopeGuard([&]{
            if(previous.isNull())qunsetenv("PLATINUM_PERSONAL_ASSETS");
            else qputenv("PLATINUM_PERSONAL_ASSETS",previous);
            QIcon::setThemeSearchPaths(oldPaths);QIcon::setThemeName(oldTheme);
        });
        qputenv("PLATINUM_PERSONAL_ASSETS",root.path().toUtf8());
        const auto write=[](const QString &path,int size,QColor color) {
            QImage image(size,size,QImage::Format_ARGB32);image.fill(Qt::transparent);
            const int inset=size/4;
            QPainter p(&image);p.fillRect(inset,inset,size-2*inset,size-2*inset,color);p.end();return image.save(path);
        };
        const QColor normal32(200,120,80),normal16(80,160,240),selected32(30,210,70),selected16(240,180,30);
        QVERIFY(write(root.filePath("icons/folder.png"),32,normal32));
        QVERIFY(write(root.filePath("icons/folder-16.png"),16,normal16));
        QVERIFY(write(root.filePath("icons/folder-selected.png"),32,selected32));
        // A malformed-size small variant must neither replace the 32px state
        // nor silently apply that state to the list view.
        QVERIFY(write(root.filePath("icons/folder-16-selected.png"),32,QColor(200,20,240)));
        const auto imported=[&]{
            QFileSystemModel files;installPlatinumIcons(&files);
            return files.iconProvider()->icon(QFileInfo(root.path()));
        };
        QStandardItemModel model(1,1);const auto index=model.index(0,0);
        model.setData(index,"Owned folder");model.setData(index,imported(),Qt::DecorationRole);
        const auto matches=[&](QAbstractItemView &view,QColor normal,QColor selected) {
            view.selectionModel()->clearSelection();const auto before=view.viewport()->grab().toImage();
            view.selectionModel()->select(index,QItemSelectionModel::ClearAndSelect);
            const auto after=view.viewport()->grab().toImage();if(before.size()!=after.size())return false;
            int count=0;
            for(int y=0;y<before.height();++y)for(int x=0;x<before.width();++x)
                if(before.pixelColor(x,y)==normal) {
                    ++count;if(after.pixelColor(x,y)!=selected)return false;
                }
            return count>=20;
        };
        FolderIcons icons;icons.resize(320,220);icons.setModel(&model);icons.show();
        QVERIFY(QTest::qWaitForWindowExposed(&icons));icons.activateWindow();QTRY_VERIFY(icons.isActiveWindow());
        QVERIFY(matches(icons,normal32,selected32));icons.hide();
        FolderList list;list.resize(320,180);list.setModel(&model);list.show();
        QVERIFY(QTest::qWaitForWindowExposed(&list));list.activateWindow();QTRY_VERIFY(list.isActiveWindow());
        // Qt normally chooses the 32px source at DPR1.5 for a 16px slot.
        const auto listNormal=list.devicePixelRatioF()>1 ? normal32 : normal16;
        const auto listHalf=list.devicePixelRatioF()>1 ? QColor(100,60,40) : QColor(40,80,120);
        QVERIFY(matches(list,listNormal,listHalf));
        QVERIFY(write(root.filePath("icons/folder-16-selected.png"),16,selected16));
        model.setData(index,imported(),Qt::DecorationRole);
        QVERIFY(matches(list,listNormal,selected16));list.hide();
        // Theme engines can synthesize a Selected state. Keep the generic RGB
        // rule for named theme artwork instead of mistaking it for supplied art.
        QVERIFY(QDir(root.path()).mkpath("themes/owned-selection/32x32/places"));
        QFile theme(root.filePath("themes/owned-selection/index.theme"));QVERIFY(theme.open(QIODevice::WriteOnly));
        theme.write("[Icon Theme]\nName=Owned selection\nDirectories=32x32/places\n"
                    "[32x32/places]\nSize=32\nType=Fixed\nContext=Places\n");theme.close();
        const QColor themed(160,80,200);
        QVERIFY(write(root.filePath("themes/owned-selection/32x32/places/owned-folder.png"),32,themed));
        QIcon::setThemeSearchPaths({root.filePath("themes")});QIcon::setThemeName("owned-selection");
        const auto themeIcon=QIcon::fromTheme("owned-folder");QVERIFY(!themeIcon.isNull());
        model.setData(index,themeIcon,Qt::DecorationRole);icons.show();
        QVERIFY(QTest::qWaitForWindowExposed(&icons));icons.activateWindow();QTRY_VERIFY(icons.isActiveWindow());
        QVERIFY(matches(icons,themed,QColor(80,40,100)));
    }
    void selectedListIconsHalveRgbPreserveTransparencyAndRefresh()
    {
        QStandardItemModel model(1,1);
        model.setData(model.index(0,0),"Owned icon");
        const auto icon=[](QColor color) {
            QPixmap image(16,16);image.fill(Qt::transparent);
            QPainter painter(&image);painter.fillRect(3,3,10,10,color);painter.end();
            return QIcon(image);
        };
        model.setData(model.index(0,0),icon(QColor(200,120,80)),Qt::DecorationRole);
        FolderList view;view.resize(320,180);view.setModel(&model);view.show();
        QVERIFY(QTest::qWaitForWindowExposed(&view));view.activateWindow();QTRY_VERIFY(view.isActiveWindow());
        const auto index=model.index(0,0);
        view.selectionModel()->clearSelection();
        const auto normal=view.viewport()->grab().toImage();
        QList<QPoint> opaque;
        for(int y=0;y<normal.height();++y)for(int x=0;x<normal.width();++x)
            if(normal.pixelColor(x,y)==QColor(200,120,80))opaque.push_back({x,y});
        QVERIFY(opaque.size()>=25);
        view.selectionModel()->select(index,QItemSelectionModel::ClearAndSelect);
        const auto selected=view.viewport()->grab().toImage();
        for(const auto &point:opaque)QCOMPARE(selected.pixelColor(point),QColor(100,60,40));
        const auto transparent=opaque.first()-QPoint(qRound(2*normal.devicePixelRatio()),0);
        QCOMPARE(normal.pixelColor(transparent),QColor("#ddd"));
        QCOMPARE(selected.pixelColor(transparent),normal.pixelColor(transparent));
        // The row outside the compact icon/name decorations remains unchanged.
        QCOMPARE(selected.pixelColor(normal.width()-8,2),normal.pixelColor(normal.width()-8,2));
        model.setData(index,icon(QColor(80,160,240)),Qt::DecorationRole);
        const auto refreshed=view.viewport()->grab().toImage();
        for(const auto &point:opaque)QCOMPARE(refreshed.pixelColor(point),QColor(40,80,120));
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!view.isActiveWindow());
        const auto inactive=view.viewport()->grab().toImage();
        for(const auto &point:opaque)QCOMPARE(inactive.pixelColor(point),QColor(80,160,240));
        QVERIFY(view.selectionModel()->isSelected(index));
    }
    void finderColumnHeadersMatchMeasuredBevelsAndKeepSorting()
    {
        QTemporaryDir root,profile;QVERIFY(QDir(root.path()).mkdir("Inside"));
        QPointer<FinderWindow> window=new FinderWindow(root.path(),profile.path(),root.path());
        const auto close=qScopeGuard([&]{if(window)window->close();});
        window->setViewMode(true);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        auto *view=window->findChild<FolderList*>();QVERIFY(view);auto *header=view->header();
        const auto check=[&](int logical,bool active,bool down){
            const auto image=header->viewport()->grab().toImage();const auto ratio=image.devicePixelRatio();
            const int x=header->sectionViewportPosition(logical)+3;
            for(int y=0;y<21;++y){
                const auto expected=QColor(active ?
                    (y==0 ? (down?"#111":"#666") : y==1 ? (down?"#555":"#fff") :
                     y==19 ? (down?"#aaa":"#888") : y==20 ? (down?"#444":"#333") : (down?"#888":"#ccc")) :
                    (y==0 ? (down?"#777":"#aaa") : y==20 ? "#999" : (down?"#bbb":"#ddd")));
                if(image.pixelColor(int((x+.5)*ratio),int((y+.5)*ratio))!=expected)return false;
            }
            return true;
        };
        QTRY_VERIFY(check(0,true,true));QTRY_VERIFY(check(3,true,false));
        QVERIFY(header->sectionsClickable());QVERIFY(header->sectionsMovable());
        const auto point=QPoint(header->sectionViewportPosition(3)+header->sectionSize(3)/2,10);
        QTest::mouseClick(header->viewport(),Qt::LeftButton,{},point);
        QTRY_COMPARE(header->sortIndicatorSection(),3);
        QTRY_VERIFY(check(0,true,false));QTRY_VERIFY(check(3,true,true));
        const auto order=header->sortIndicatorOrder();
        QTest::mouseClick(header->viewport(),Qt::LeftButton,{},point);
        QTRY_VERIFY(header->sortIndicatorOrder()!=order);
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QTRY_VERIFY(check(0,false,false));QTRY_VERIFY(check(3,false,true));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_VERIFY(check(0,true,false));QTRY_VERIFY(check(3,true,true));
    }
    void closedFolderDisclosureMatchesReferenceAndStillExpandsRealChildren()
    {
        QTemporaryDir root,profile;QVERIFY(QDir(root.path()).mkpath("Inside/Child"));
        QPointer<FinderWindow> window=new FinderWindow(root.path(),profile.path(),root.path());
        const auto close=qScopeGuard([&]{if(window)window->close();});
        window->setViewMode(true);window->show();QVERIFY(QTest::qWaitForWindowExposed(window));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *view=window->findChild<FolderList*>();QVERIFY(view);
        auto *model=qobject_cast<QFileSystemModel*>(view->model());QVERIFY(model);
        const auto index=model->index(root.path()+"/Inside");QVERIFY(index.isValid());
        model->fetchMore(index);QTRY_VERIFY(model->rowCount(index)>0);
        const auto row=view->visualRect(index);QVERIFY(row.isValid());
        const QPoint origin(row.left()-view->indentation()/2-2,row.center().y()-6);
        const auto check=[&]{
            const auto image=view->viewport()->grab().toImage();const auto ratio=image.devicePixelRatio();
            const auto at=[&](int x,int y){return image.pixelColor(int((origin.x()+x+.5)*ratio),int((origin.y()+y+.5)*ratio));};
            return at(0,0)==QColor(Qt::black) && at(1,0)==QColor("#ddd") &&
                at(1,2)==QColor("#ccccff") && at(2,3)==QColor("#9999ff") &&
                at(4,5)==QColor("#6666cc") && at(5,6)==QColor("#888") &&
                at(6,6)==QColor("#bbb") && at(1,11)==QColor("#bbb");
        };
        const auto expandedCheck=[&]{
            const auto image=view->viewport()->grab().toImage();const auto ratio=image.devicePixelRatio();
            const QPoint openOrigin=origin+QPoint(-4,4);
            const auto at=[&](int x,int y){return image.pixelColor(int((openOrigin.x()+x+.5)*ratio),int((openOrigin.y()+y+.5)*ratio));};
            return at(0,0)==QColor(Qt::black) && at(10,0)==QColor(Qt::black) &&
                at(11,0)==QColor("#ddd") && at(2,1)==QColor("#ccccff") &&
                at(8,1)==QColor("#9999ff") && at(7,2)==QColor("#6666cc") &&
                at(10,1)==QColor("#888") && at(11,1)==QColor("#bbb") &&
                at(6,6)==QColor("#bbb");
        };
        QTRY_VERIFY(check());
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},origin+QPoint(2,5));
        QTRY_VERIFY(view->isExpanded(index));
        QTRY_VERIFY(expandedCheck());
        const auto child=model->index(root.path()+"/Inside/Child");QVERIFY(child.isValid());
        QTRY_VERIFY(view->viewport()->rect().intersects(view->visualRect(child)));
        // The wide open triangle's left edge still reaches Qt's real toggle region.
        QTest::mouseClick(view->viewport(),Qt::LeftButton,{},origin+QPoint(-3,4));
        QTRY_VERIFY(!view->isExpanded(index));QTRY_VERIFY(check());
        view->setCurrentIndex(index);view->setFocus();QTest::keyClick(view,Qt::Key_Right);
        QTRY_VERIFY(view->isExpanded(index));QTRY_VERIFY(expandedCheck());
        view->setCurrentIndex(index);view->setFocus();QTest::keyClick(view,Qt::Key_Left);
        QTRY_VERIFY(!view->isExpanded(index));QTRY_VERIFY(check());
        QVERIFY(view->selectionModel()->isSelected(index));
        QWidget other;other.show();QVERIFY(QTest::qWaitForWindowExposed(&other));
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QTRY_VERIFY(check());QVERIFY(view->selectionModel()->isSelected(index));
        window->activateWindow();QTRY_VERIFY(window->isActiveWindow());
        view->setFocus();QTest::keyClick(view,Qt::Key_Right);
        QTRY_VERIFY(view->isExpanded(index));QTRY_VERIFY(expandedCheck());
        other.activateWindow();QTRY_VERIFY(!window->isActiveWindow());
        QTRY_VERIFY(expandedCheck());QVERIFY(view->selectionModel()->isSelected(index));
    }
    void cutCopyAndPasteFilesBetweenWindowsAndFileManagers()
    {
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("src") && QDir(root.path()).mkpath("dst"));
        for (const auto *name : {"A.txt", "B.txt", "C.txt"})
        {
            QFile f(root.path() + "/src/" + name);
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write(name) > 0);
        }
        QPointer<FinderWindow> source = new FinderWindow(root.path(), profile.path(), root.path() + "/src");
        QPointer<FinderWindow> target = new FinderWindow(root.path(), profile.path(), root.path() + "/dst");
        const auto close = qScopeGuard([&] { if (source) source->close(); if (target) target->close(); });
        source->show();
        target->show();
        QTRY_VERIFY(source->directoryState().value("loaded").toBool() && target->directoryState().value("loaded").toBool());
        auto *icons = source->findChild<FolderIcons *>();
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto select = [&](const QString &name)
        {
            icons->selectionModel()->clearSelection();
            icons->selectionModel()->select(model->index(root.path() + "/src/" + name), QItemSelectionModel::Select);
        };
        const auto trigger = [](FinderWindow *window, const char *name)
        {
            auto *action = window->findChild<QAction *>(name);
            QVERIFY(action);
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
        };
        // Copy: names as text (Mac OS 9) and the item itself for any file manager.
        QTRY_VERIFY(model->index(root.path() + "/src/A.txt").isValid());
        select("A.txt");
        trigger(source, "copy");
        const auto *mime = QGuiApplication::clipboard()->mimeData();
        QCOMPARE(mime->text(), QString("A.txt"));
        QCOMPARE(mime->urls(), QList<QUrl>({QUrl::fromLocalFile(root.path() + "/src/A.txt")}));
        QVERIFY(mime->data("x-special/gnome-copied-files").startsWith("copy\n"));
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/A.txt"));
        QVERIFY(QFileInfo::exists(root.path() + "/src/A.txt"));
        // Cut then Paste moves, and the clipboard no longer offers the moved item.
        select("B.txt");
        trigger(source, "cut");
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/B.txt"));
        QTRY_VERIFY(!QFileInfo::exists(root.path() + "/src/B.txt"));
        QTRY_VERIFY(!QGuiApplication::clipboard()->mimeData() || !QGuiApplication::clipboard()->mimeData()->hasUrls());
        // Another file manager's Cut (KDE's marker) pastes as a move too.
        auto *foreign = new QMimeData;
        foreign->setUrls({QUrl::fromLocalFile(root.path() + "/src/C.txt")});
        foreign->setData("application/x-kde-cutselection", "1");
        QGuiApplication::clipboard()->setMimeData(foreign);
        trigger(target, "paste");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/dst/C.txt") && !QFileInfo::exists(root.path() + "/src/C.txt"));
        // The move is visible before its worker reports back; closing a busy window asks the user to wait.
        QTRY_VERIFY(!target->directoryState().value("busy").toBool());
    }
    void aDragHeldOverAFolderSpringsItOpen()
    {
        // Spring-loaded folders (Mac OS 9): holding a drag over a folder for a second opens it in
        // the window, so the drop can go further in; moving away first does not.
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("Projects/Inner"));
        {
            QFile f(root.path() + "/Report.txt");
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write("report") > 0);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *icons = window->findChild<FolderIcons *>();
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        QTRY_VERIFY(model->index(root.path() + "/Projects").isValid());
        QTRY_VERIFY(icons->visualRect(model->index(root.path() + "/Projects")).isValid());
        QTRY_VERIFY(icons->visualRect(model->index(root.path() + "/Report.txt")).isValid());
        // Where each item is now: the view sorts again once the folder has loaded.
        const auto folderAt = [&] { return icons->visualRect(model->index(root.path() + "/Projects")).center(); };
        const auto fileAt = [&] { return icons->visualRect(model->index(root.path() + "/Report.txt")).center(); };
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(profile.path() + "/elsewhere.txt")});
        const auto move = [&](QPoint at) {
            QDragMoveEvent event(at, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, {});
            QCoreApplication::sendEvent(icons->viewport(), &event);
        };
        QDragEnterEvent enter(fileAt(), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, {});
        QCoreApplication::sendEvent(icons->viewport(), &enter);
        // Over the folder briefly, then off it: nothing opens.
        move(folderAt());
        QTest::qWait(SpringLoading::delay / 2);
        QCOMPARE(model->filePath(icons->indexAt(fileAt())), root.path() + "/Report.txt");
        move(fileAt());
        QTest::qWait(SpringLoading::delay);
        QCOMPARE(window->directory(), root.path());
        // Held over it: it opens in this window.
        move(folderAt());
        QTRY_COMPARE_WITH_TIMEOUT(window->directory(), root.path() + "/Projects", 3000);
        QDragLeaveEvent leave;
        QCoreApplication::sendEvent(icons->viewport(), &leave);
    }
    void labelsColourItemsAndShowInGetInfo()
    {
        // File ▸ Label (Mac OS 9): None and seven colours, kept with the file.
        QTemporaryDir root, profile;
        {
            QFile f(root.path() + "/Report.txt");
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write("report") > 0);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        auto *icons = window->findChild<FolderIcons *>();
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto path = root.path() + "/Report.txt";
        QTRY_VERIFY(model->index(path).isValid());
        icons->selectionModel()->select(model->index(path), QItemSelectionModel::ClearAndSelect);
        auto *red = window->findChild<QAction *>("label2");
        QVERIFY(red);
        QCOMPARE(red->text(), QString("Red"));
        red->trigger();
        QCOMPARE(FileLabels::label(path, profile.path()), 2);
        QTRY_COMPARE(model->data(model->index(path), Qt::ForegroundRole).value<QColor>(), QColor("#dd0907"));
        auto *menu = window->findChild<QMenu *>("labelMenu");
        QVERIFY(menu);
        QMetaObject::invokeMethod(menu, "aboutToShow");
        QVERIFY(red->isChecked());
        QVERIFY(!window->findChild<QAction *>("label0")->isChecked());
        // Get Info shows it.
        window->findChild<QAction *>("getInfo")->trigger();
        QWidget *info = nullptr;
        QTRY_VERIFY((info = [&]() -> QWidget * {
            for (auto *w : QApplication::topLevelWidgets())
                if (w->objectName() == "infoWindow" && w->isVisible()) return w;
            return nullptr; }()));
        QCOMPARE(info->findChild<QLabel *>("infoLabel")->text(), QString("Red"));
        info->close();
        // None removes it.
        window->findChild<QAction *>("label0")->trigger();
        QCOMPARE(FileLabels::label(path, profile.path()), 0);
        QTRY_VERIFY(!model->data(model->index(path), Qt::ForegroundRole).value<QColor>().isValid() ||
                    model->data(model->index(path), Qt::ForegroundRole).value<QColor>() != QColor("#dd0907"));
    }
    void rightClickMenusOfferTheFileCommandsLikeDolphin()
    {
        // Lily Hewitt, 2026-10-29: Finder should be as usable as Dolphin. Right-click is where
        // Cut, Copy and Paste are looked for.
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("Folder"));
        {
            QFile f(root.path() + "/Note.txt");
            QVERIFY(f.open(QIODevice::WriteOnly) && f.write("note") > 0);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { if (window) window->close(); });
        window->show();
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>();
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        QTRY_VERIFY(model->index(root.path() + "/Note.txt").isValid() && model->index(root.path() + "/Folder").isValid());
        const auto menuFor = [&](const QString &name) -> QStringList
        {
            for (auto *old : window->findChildren<QMenu *>("finderContextMenu"))
                old->close();
            if (!QTest::qWaitFor([&] { return window->findChildren<QMenu *>("finderContextMenu").isEmpty(); }))
                return {};
            QPoint point(icons->viewport()->width() - 5, icons->viewport()->height() - 5);
            if (!name.isEmpty())
                point = icons->visualRect(model->index(root.path() + "/" + name)).center();
            emit icons->customContextMenuRequested(point);
            QMenu *menu = nullptr;
            if (!QTest::qWaitFor([&] {
                    for (auto *m : window->findChildren<QMenu *>("finderContextMenu"))
                        if (m->isVisible())
                            menu = m;
                    return menu != nullptr; }))
                return {};
            QStringList texts;
            for (auto *action : menu->actions())
                texts << (action->isSeparator() ? QString("-") : action->text() + (action->isEnabled() ? "" : " (off)"));
            menu->close();
            return texts;
        };
        const auto onFile = menuFor("Note.txt");
        for (const auto &command : {"Open", "Cut", "Copy", "Copy Path", "Get Info…", "Rename…", "Duplicate",
                                    "Make Alias", "Move to Trash"})
            QVERIFY2(onFile.contains(command), qPrintable(onFile.join(", ")));
        QVERIFY(!onFile.contains("Paste Into Folder") && !onFile.contains("Open in Terminal"));
        const auto onFolder = menuFor("Folder");
        QVERIFY2(onFolder.contains("Paste Into Folder (off)") && onFolder.contains("Open in Terminal"),
                 qPrintable(onFolder.join(", ")));
        const auto onBackground = menuFor({});
        for (const auto &command : {"New Folder", "New Text File", "Paste (off)", "Open in Terminal", "Select All", "Show Hidden Files"})
            QVERIFY2(onBackground.contains(command), qPrintable(onBackground.join(", ")));
        const auto trigger = [&](const char *name)
        {
            auto *action = window->findChild<QAction *>(name);
            QVERIFY(action);
            QTRY_VERIFY(action->isEnabled());
            action->trigger();
        };
        const auto select = [&](const QString &name)
        {
            icons->selectionModel()->clearSelection();
            icons->selectionModel()->select(model->index(root.path() + "/" + name), QItemSelectionModel::Select);
        };
        // Copy, then Paste Into Folder on the folder's own menu.
        select("Note.txt");
        trigger("copy");
        const auto intoFolder = menuFor("Folder");
        QVERIFY2(intoFolder.contains("Paste Into Folder"), qPrintable(intoFolder.join(", ")));
        // Open the folder's menu again and choose it.
        for (auto *old : window->findChildren<QMenu *>("finderContextMenu"))
            old->close();
        QTRY_VERIFY(window->findChildren<QMenu *>("finderContextMenu").isEmpty());
        emit icons->customContextMenuRequested(icons->visualRect(model->index(root.path() + "/Folder")).center());
        QAction *into = nullptr;
        QTRY_VERIFY((into = window->findChild<QAction *>("pasteInto")) && into->isEnabled());
        into->trigger();
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/Folder/Note.txt"));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        // Copy Path puts the path on the clipboard as text.
        select("Note.txt");
        trigger("copyPath");
        QCOMPARE(QGuiApplication::clipboard()->text(), root.path() + "/Note.txt");
        // Make Alias: a link beside it. New Text File: untitled.txt, then untitled 2.txt.
        trigger("makeAlias");
        QTRY_VERIFY(QFileInfo(root.path() + "/Note.txt alias").isSymLink());
        QCOMPARE(QFileInfo(root.path() + "/Note.txt alias").symLinkTarget(), root.path() + "/Note.txt");
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        trigger("newFile");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/untitled.txt"));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        trigger("newFile");
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/untitled 2.txt"));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        // The Delete key moves the selection to the Trash, as in Dolphin.
        QVERIFY(window->findChild<QAction *>("trash")->shortcuts().contains(QKeySequence(Qt::Key_Delete)));
    }
    void findListsMatchingNamesButNotHiddenFoldersOrLinkedOnes()
    {
        QTemporaryDir root, profile, outside;
        for (const auto *folder : {"A", "B", ".hidden"})
            QVERIFY(QDir(root.path()).mkdir(folder));
        for (const auto *file : {"A/Report.txt", "B/annual report.pdf", ".hidden/report.txt", "B/notes.txt"})
        {
            QFile f(root.path() + "/" + file);
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        {
            QFile f(outside.path() + "/report outside.txt");
            QVERIFY(f.open(QIODevice::WriteOnly));
        }
        QVERIFY(QFile::link(outside.path(), root.path() + "/linked"));
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { if (window) window->close(); });
        window->findItems("report");
        QWidget *results = nullptr;
        QTRY_VERIFY((results = [] { for (auto *w : QApplication::topLevelWidgets()) if (w->objectName() == "findWindow") return w; return (QWidget *)nullptr; }()));
        const auto closeResults = qScopeGuard([&] { results->close(); });
        auto *list = results->findChild<QTreeWidget *>("findResults");
        QTRY_VERIFY(results->findChild<QLabel *>("findStatus")->text().contains("found"));
        QStringList names;
        for (int i = 0; i < list->topLevelItemCount(); ++i)
            names << list->topLevelItem(i)->text(0);
        names.sort();
        QCOMPARE(names, QStringList({"Report.txt", "annual report.pdf"}));
        QSignalSpy documents(window.data(), &FinderWindow::openDocument);
        emit list->itemDoubleClicked(list->topLevelItem(0), 0);
        QCOMPARE(documents.size(), 1);
    }
    void specialMenuEndsTheSessionOnlyInsideTheDesktop()
    {
        QTemporaryDir root, profile;
        const auto names = [](FinderWindow *window)
        {
            QStringList result;
            for (auto *menu : window->menuBar()->findChildren<QMenu *>())
                if (menu->title() == "Special")
                    for (auto *item : menu->actions())
                        if (item->isVisible())
                            result << (item->isSeparator() ? "-" : item->objectName());
            return result;
        };
        {
            // A Finder on its own cannot end anyone's session.
            FinderWindow alone(root.path(), profile.path(), root.path());
            QCOMPARE(names(&alone), QStringList({"emptyTrash"}));
        }
        qputenv("FRANKENSTEIN_ISOLATED", "1");
        const auto restore = qScopeGuard([] { qunsetenv("FRANKENSTEIN_ISOLATED"); });
        FinderWindow window(root.path(), profile.path(), root.path());
        QCOMPARE(names(&window), QStringList({"emptyTrash", "-", "logOut"}));
        window.setPowerAvailable(true);
        QCOMPARE(names(&window), QStringList({"emptyTrash", "-", "sleep", "restart", "shutDown", "-", "logOut"}));
        QSignalSpy requests(&window, &FinderWindow::sessionRequested);
        for (const auto *name : {"sleep", "restart", "shutDown", "logOut"})
            window.findChild<QAction *>(name)->trigger();
        QCOMPARE(requests.count(), 4);
        QStringList commands;
        for (const auto &request : requests)
            commands << request.at(0).toString();
        QCOMPARE(commands, QStringList({"sleep", "restart", "shut-down", "log-out"}));
    }
    void finderIconsShowPictureThumbnails()
    {
        // A picture shows itself (a framed thumbnail); a folder keeps the Platinum folder.
        QTemporaryDir root;
        QImage picture(120, 60, QImage::Format_RGB32);
        picture.fill(QColor(220, 20, 30));
        QVERIFY(picture.save(root.path() + "/Red Card.png"));
        QVERIFY(QDir(root.path()).mkdir("Folder"));
        QFileSystemModel model;
        installPlatinumIcons(&model);
        model.setRootPath(root.path());
        const auto image = [&](const QString &name)
        { return model.fileIcon(model.index(root.path() + "/" + name)).pixmap(64, 64).toImage(); };
        QTRY_COMPARE(image("Red Card.png").pixelColor(32, 32), QColor(220, 20, 30));
        QVERIFY(image("Folder").pixelColor(32, 32) != QColor(220, 20, 30));
    }

    void goMenuOpensStandardFoldersAndMountedDrives()
    {
        // Go (native adaptation): Macintosh HD's Desktop, and each mounted drive, shown in this window
        // as in Dolphin; drives come and go as they are mounted; nothing outside the places appears.
        QTemporaryDir root, profile, drives;
        QVERIFY(QDir(root.path()).mkdir("Desktop") && QDir(drives.path()).mkdir("USB STICK"));
        FinderPlaces::volumes = {drives.path()};
        const auto reset = qScopeGuard([] { FinderPlaces::volumes.clear(); });
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { if (window) window->close(); });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QSignalSpy opened(window.data(), &FinderWindow::openFolder);
        auto *desktop = window->findChild<QAction *>("goDesktop");
        QVERIFY(desktop && window->findChild<QAction *>("goHome") && window->findChild<QAction *>("goToFolder"));
        desktop->trigger();
        QCOMPARE(window->directory(), QFileInfo(root.path() + "/Desktop").canonicalFilePath());
        QCOMPARE(opened.size(), 0);
        QMenu *go = nullptr;
        for (auto *menu : window->menuBar()->findChildren<QMenu *>())
            if (menu->title() == "Go")
                go = menu;
        QVERIFY(go);
        emit go->aboutToShow();
        QList<QAction *> found;
        for (auto *entry : go->actions())
            if (entry->objectName() == "goDrive")
                found << entry;
        QCOMPARE(found.size(), 1);
        QCOMPARE(found.first()->text(), QString("USB STICK"));
        found.first()->trigger();
        QCOMPARE(window->directory(), QFileInfo(drives.path() + "/USB STICK").canonicalFilePath());
        window->findChild<QAction *>("goHome")->trigger(); // Leave the drive before it is unmounted.
        // Unmounted: gone the next time the menu opens.
        QVERIFY(QDir(drives.path() + "/USB STICK").removeRecursively());
        emit go->aboutToShow();
        for (auto *entry : go->actions())
            QVERIFY(entry->objectName() != "goDrive");
    }

    void finderWindowMenuListsFoldersAndEmptyTrashConfirms()
    {
        // docs/historical-fidelity-review.md F07: Window lists open Finder windows;
        // Special > Empty Trash… defaults to Cancel; Finder offers no Quit.
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("Alpha") && QDir(root.path()).mkpath("Beta"));
        FinderService finder(root.path(), profile.path());
        QVERIFY(finder.OpenDirectory(root.path() + "/Alpha"));
        QVERIFY(finder.OpenDirectory(root.path() + "/Beta"));
        FinderWindow *alpha = nullptr;
        for (auto *widget : QApplication::topLevelWidgets())
            if (auto *window = qobject_cast<FinderWindow *>(widget);
                window && window->directory().endsWith("/Alpha"))
                alpha = window;
        QVERIFY(alpha);
        QVERIFY(!alpha->findChild<QAction *>("quit"));
        QMenu *windowMenu = nullptr;
        for (auto *menu : alpha->menuBar()->findChildren<QMenu *>())
            if (menu->title() == "Window")
                windowMenu = menu;
        QVERIFY(windowMenu);
        emit windowMenu->aboutToShow();
        emit windowMenu->aboutToShow(); // Rebuilt, not appended twice.
        QStringList listed;
        QAction *checked = nullptr;
        for (auto *action : windowMenu->actions())
            if (action->objectName().startsWith("window:"))
            {
                listed << QFileInfo(action->objectName().mid(7)).fileName();
                if (action->isChecked())
                    checked = action;
            }
        QCOMPARE(listed, (QStringList{"Alpha", "Beta"}));
        QVERIFY(checked && checked->objectName().endsWith("/Alpha"));
        // Empty Trash: Cancel by default, then OK removes the trashed folder.
        FileOperations operations(root.path(), profile.path() + "/data/Trash");
        QVERIFY(QDir(root.path()).mkpath("Old"));
        QVERIFY(operations.trash(root.path() + "/Old").ok);
        auto *empty = alpha->findChild<QAction *>("emptyTrash");
        QVERIFY(empty);
        bool cancelWasDefault = false, returnWasDelivered = false, focusOnOk = false;
        const auto answer = [&](bool accept)
        {
            // Poll until the modal question exists; a fixed delay can fire before
            // exec() shows it and leave the test blocked forever.
            auto *poll = new QTimer(this);
            poll->setInterval(20);
            connect(poll, &QTimer::timeout, poll, [poll, accept, &cancelWasDefault, &returnWasDelivered, &focusOnOk]
            {
                for (auto *widget : QApplication::topLevelWidgets())
                    if (auto *box = qobject_cast<QMessageBox *>(widget); box && box->isVisible())
                    {
                        poll->stop();
                        poll->deleteLater();
                        cancelWasDefault = box->defaultButton() == box->button(QMessageBox::Cancel);
                        if (accept)
                            box->button(QMessageBox::Ok)->click();
                        else if (focusOnOk)
                        {
                            // The destructive button has focus (Tab, or a pointer pressed on it), and
                            // Return still means the ringed default, Cancel, as in Mac OS 9.
                            box->button(QMessageBox::Ok)->setFocus(Qt::TabFocusReason);
                            returnWasDelivered = box->button(QMessageBox::Ok)->hasFocus();
                            QTest::keyClick(box->button(QMessageBox::Ok), Qt::Key_Return);
                        }
                        else if (auto *focused = box->focusWidget(); focused && focused->hasFocus())
                        {
                            qInfo() << "Empty Trash initial focus" << focused->objectName()
                                    << "default" << box->defaultButton()->text();
                            returnWasDelivered = true;
                            QTest::keyClick(focused, Qt::Key_Return);
                        }
                        else
                            box->reject(); // Fail the focus assertion without trapping the test.
                        return;
                    }
            });
            QTimer::singleShot(10000, poll, [poll] { poll->stop(); poll->deleteLater(); });
            poll->start();
        };
        answer(false);
        empty->trigger();
        QVERIFY(cancelWasDefault);
        QVERIFY(returnWasDelivered);
        QCOMPARE(operations.trashedItems().size(), 1);
        focusOnOk = true;
        returnWasDelivered = false;
        answer(false);
        empty->trigger();
        QVERIFY(returnWasDelivered);
        QCOMPARE(operations.trashedItems().size(), 1); // Return with OK focused kept the Trash.
        focusOnOk = false;
        answer(true);
        empty->trigger();
        QTRY_VERIFY(operations.trashedItems().isEmpty());
        // The worker's completion clears the busy state; closing earlier is refused.
        QTRY_VERIFY(alpha->findChildren<QProgressDialog *>().isEmpty());
        for (auto *widget : QApplication::topLevelWidgets())
            if (qobject_cast<FinderWindow *>(widget))
                widget->close();
    }
    void initTestCase()
    {
        if (qEnvironmentVariableIsSet("PLATINUM_TEST_REQUIRE_KDE_THEME"))
            QVERIFY2(kdePlatformThemeLoaded(), "The actual KDE Qt platform theme is not loaded; fallback cannot qualify this test.");
        QApplication::setStyle(new PlatinumStyle);
        QApplication::setFont(classicInterfaceFont());
    }
    void folderBackgroundDragSelectsMultipleFiles_data()
    {
        QTest::addColumn<bool>("list");
        QTest::newRow("icons") << false;
        QTest::newRow("list") << true;
    }
    void folderBackgroundDragSelectsMultipleFiles()
    {
        QFETCH(bool, list);
        QTemporaryDir root, profile;
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(root.path() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        window->setViewMode(list);
        auto *view = list ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>())
                          : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>());
        auto *model = qobject_cast<QFileSystemModel *>(view->model());
        const auto one = model->index(root.path() + "/One.txt");
        const auto two = model->index(root.path() + "/Two.txt");
        QTRY_VERIFY(view->visualRect(one).isValid() && view->visualRect(two).isValid());
        const auto bounds = view->visualRect(one).united(view->visualRect(two));
        const QPoint start(std::min(view->viewport()->width() - 5, bounds.right() + 20),
                           bounds.bottom() + 30);
        QVERIFY(!view->indexAt(start).isValid());
        QTest::mousePress(view->viewport(), Qt::LeftButton, {}, start);
        QTest::mouseMove(view->viewport(), bounds.topLeft() + QPoint(2, 2), 40);
        QTest::mouseRelease(view->viewport(), Qt::LeftButton, {}, bounds.topLeft() + QPoint(2, 2));
        QSet<QString> selected;
        for (const auto &index : view->selectionModel()->selectedIndexes())
            selected.insert(model->filePath(index.siblingAtColumn(0)));
        QCOMPARE(selected, (QSet<QString>{root.path() + "/One.txt", root.path() + "/Two.txt"}));
        if (!list)
            QVERIFY(static_cast<FolderIcons *>(view)->isSelectionRectVisible());
        QVERIFY(window->grab().save(QCoreApplication::applicationDirPath() +
                                    "/artifacts/finder-marquee-" + (list ? "list" : "icons") +
                                    ".png"));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void folderDropTargetsUseTheVisibleViewportAndUpdateTheItemCount()
    {
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkdir("Inside"));
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        QCOMPARE(finderSurfaceId(nullptr), quint32(0));
        QCOMPARE(finderSurfaceId(window->windowHandle()), quint32(0)); // Offscreen is unsupported.
        auto *count = window->findChild<QLabel *>("folderItemCount");
        QVERIFY(count);
        QTRY_VERIFY2(count->text().startsWith("1 item, "), qPrintable(count->text()));
        for (bool list : {false, true})
        {
            window->setViewMode(list);
            auto *view = list
                             ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>())
                             : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>());
            auto *model = qobject_cast<QFileSystemModel *>(view->model());
            QVERIFY(model);
            const auto child = model->index(root.path() + "/Inside");
            QTRY_VERIFY(view->visualRect(child).isValid());
            const auto childPoint =
                view->viewport()->mapTo(window, view->visualRect(child).center());
            QCOMPARE(window->dropDirectoryAt(childPoint), root.path() + "/Inside");
            const auto empty =
                view->viewport()->mapTo(window, QPoint(20, view->viewport()->height() - 20));
            QCOMPARE(window->dropDirectoryAt(empty), root.path());
            QVERIFY(window->dropDirectoryAt({-10, -10}).isEmpty());
            QVERIFY(
                window->dropDirectoryAt(count->mapTo(window, count->rect().center())).isEmpty());
        }
        QFile added(root.path() + "/New.txt");
        QVERIFY(added.open(QIODevice::WriteOnly));
        added.write("new");
        added.close();
        QTRY_VERIFY(count->text().startsWith("2 items, "));
        QVERIFY(count->text().endsWith(" available"));
        QCOMPARE(count->alignment(), Qt::Alignment(Qt::AlignCenter));
        QVERIFY(QFile::remove(added.fileName()));
        QTRY_VERIFY2(count->text().startsWith("1 item, "), qPrintable(count->text()));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void ownedAlertsKeepButtonMeaningWithoutHostThemeIcons()
    {
        QMessageBox dialog(QMessageBox::Question, "Save Changes?", "Save changes before closing?",
                           QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel);
        dialog.setDefaultButton(QMessageBox::Save);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        for (auto *button : dialog.buttons())
            QVERIFY(button->icon().isNull());
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        QVERIFY(dialog.grab().save(QCoreApplication::applicationDirPath() +
                                   "/artifacts/owned-alert.png"));
        QTest::keyClick(&dialog, Qt::Key_Escape);
        QCOMPARE(dialog.result(), int(QMessageBox::Cancel));
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QTest::keyClick(&dialog, Qt::Key_Return);
        QCOMPARE(dialog.result(), int(QMessageBox::Save));
    }
    void editMenuCopiesItemNamesAndShowsTheClipboardLikeMacOS9()
    {
        // As checked in the 9.2.2 Finder: Copy with items selected copies their names as text,
        // one per line in display order; Undo, Cut, Paste and Clear stay disabled; Show Clipboard
        // opens a "Clipboard" window describing its contents. Nothing on disk changes.
        QTemporaryDir root, profile;
        for (const auto *name : {"Alpha.txt", "Beta.txt", "Gamma.txt"})
        {
            QFile file(root.path() + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        const auto listing = [&] { return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
        const auto before = listing();
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto action = [&](const char *name) { return window->findChild<QAction *>(name); };
        for (const auto *name : {"undo", "cut", "paste", "clear"})
        {
            QVERIFY2(action(name), name);
            QVERIFY2(!action(name)->isEnabled(), name);
        }
        QVERIFY(action("copy") && action("showClipboard"));
        QTRY_VERIFY(!action("copy")->isEnabled()); // Nothing selected yet.
        for (const auto *name : {"Gamma.txt", "Alpha.txt"})
        {
            const auto index = model->index(root.path() + "/" + name);
            QTRY_VERIFY(icons->visualRect(index).isValid());
            icons->selectionModel()->select(index, QItemSelectionModel::Select);
        }
        QTRY_VERIFY(action("copy")->isEnabled());
        action("copy")->trigger();
        // Display order: by icon position, not selection order.
        const auto alpha = icons->visualRect(model->index(root.path() + "/Alpha.txt"));
        const auto gamma = icons->visualRect(model->index(root.path() + "/Gamma.txt"));
        const bool alphaFirst = alpha.y() != gamma.y() ? alpha.y() < gamma.y() : alpha.x() < gamma.x();
        const auto expected = alphaFirst ? QString("Alpha.txt\nGamma.txt") : QString("Gamma.txt\nAlpha.txt");
        QTRY_COMPARE(QGuiApplication::clipboard()->text(), expected);
        action("showClipboard")->trigger();
        const auto clipboardWindow = [] {
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->objectName() == "clipboardWindow" && widget->isVisible())
                    return widget;
            return static_cast<QWidget *>(nullptr);
        };
        QWidget *clipboard = nullptr;
        QTRY_VERIFY((clipboard = clipboardWindow()));
        QCOMPARE(clipboard->windowTitle(), QString("Clipboard"));
        QCOMPARE(clipboard->findChild<QLabel *>("clipboardDescription")->text(), QString("Clipboard contents: text"));
        QCOMPARE(clipboard->findChild<QPlainTextEdit *>("clipboardText")->toPlainText(), expected);
        // It follows the clipboard while open.
        QGuiApplication::clipboard()->setText("Beta.txt");
        QTRY_COMPARE(clipboard->findChild<QPlainTextEdit *>("clipboardText")->toPlainText(), QString("Beta.txt"));
        clipboard->close();
        QCOMPARE(listing(), before);
    }
    void getInfoOpensOneModelessInfoWindowPerItem()
    {
        // Finishing School FS-G01: the Finder's Get Info matches the desktop's (shared facts):
        // modeless, one window per item, bound to that item, closed by Escape; nothing written.
        QTemporaryDir root, profile;
        {
            QFile notes(root.path() + "/Notes.txt");
            QVERIFY(notes.open(QIODevice::WriteOnly) && notes.write("hello, world") == 12);
        }
        QVERIFY(QDir(root.path()).mkdir("Photos"));
        const auto listing = [&] { return QDir(root.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot); };
        const auto before = listing();
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        const auto cleanup = qScopeGuard([&] { delete window; });
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        const auto select = [&](const QString &name)
        {
            const auto index = model->index(root.path() + "/" + name);
            QTRY_VERIFY(icons->visualRect(index).isValid());
            icons->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        };
        const auto info = [&](const QString &title)
        {
            QList<QWidget *> found;
            for (auto *widget : QApplication::topLevelWidgets())
                if (widget->isVisible() && widget->windowTitle() == title)
                    found << widget;
            return found;
        };
        auto *getInfo = window->findChild<QAction *>("getInfo");
        QVERIFY(getInfo);
        select("Notes.txt");
        getInfo->trigger();
        QTRY_COMPARE(info("Notes.txt Info").size(), 1);
        QPointer<QWidget> notes = info("Notes.txt Info").first();
        QCOMPARE(notes->findChild<QLabel *>("infoName")->text(), QString("Notes.txt"));
        QCOMPARE(notes->findChild<QLabel *>("infoKind")->text(), QString("Plain text document"));
        QCOMPARE(notes->findChild<QLabel *>("infoSize")->text(), QString("12 bytes (12 bytes)"));
        QCOMPARE(notes->findChild<QLabel *>("infoWhere")->text(), root.path());
        QVERIFY(!QApplication::activeModalWidget());
        getInfo->trigger();
        QTest::qWait(50);
        QCOMPARE(info("Notes.txt Info").size(), 1);
        // Bound to its item: another selection opens its own window.
        select("Photos");
        getInfo->trigger();
        QTRY_COMPARE(info("Photos Info").size(), 1);
        QPointer<QWidget> photos = info("Photos Info").first();
        QCOMPARE(photos->findChild<QLabel *>("infoSize")->text(), QString("Contains 0 items"));
        QCOMPARE(notes->findChild<QLabel *>("infoName")->text(), QString("Notes.txt"));
        QVERIFY(notes->grab().save(QCoreApplication::applicationDirPath() + "/artifacts/finder-get-info.png"));
        notes->activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(notes));
        QTest::keyClick(notes, Qt::Key_Escape);
        QTRY_VERIFY(!notes);
        photos->close();
        QTRY_VERIFY(!photos);
        QCOMPARE(listing(), before);
    }
    void foldersOpenInTheSameWindowWithBackForwardAndPlacesLikeDolphin()
    {
        // Lily Hewitt, 2026-10-29: as usable as Dolphin. A folder opens in the same window; Back,
        // Forward and Enclosing Folder, the path field and the Places list move around; Option
        // still opens a folder in its own window, as on a Mac.
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkpath("Documents/Letters") && QDir(root.path()).mkpath("Desktop"));
        const auto canonical = [](const QString &path) { return QFileInfo(path).canonicalFilePath(); };
        auto *w = new FinderWindow(root.path(), profile.path(), root.path());
        const auto close = qScopeGuard([&] { w->close(); });
        w->show();
        QSignalSpy opened(w, &FinderWindow::openFolder);
        QSignalSpy navigated(w, &FinderWindow::navigated);
        auto *view = w->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(view->model());
        auto *back = w->findChild<QToolButton *>("backButton");
        auto *forward = w->findChild<QToolButton *>("forwardButton");
        auto *up = w->findChild<QToolButton *>("upButton");
        auto *path = w->findChild<QLineEdit *>("pathField");
        QVERIFY(back && forward && up && path);
        QVERIFY(!back->isEnabled() && !forward->isEnabled() && !up->isEnabled()); // Home is the top.
        const auto doubleClick = [&](const QString &name, Qt::KeyboardModifiers modifiers = {})
        {
            QTRY_VERIFY(w->directoryState().value("loaded").toBool());
            const auto index = model->index(w->directory() + "/" + name);
            QTRY_VERIFY(view->visualRect(index).isValid());
            QTest::qWait(150);
            const auto point = view->visualRect(index).center();
            QTest::mouseClick(view->viewport(), Qt::LeftButton, modifiers, point);
            QTest::mouseDClick(view->viewport(), Qt::LeftButton, modifiers, point);
        };
        doubleClick("Documents");
        QTRY_COMPARE(w->directory(), canonical(root.path() + "/Documents"));
        QCOMPARE(opened.count(), 0);
        QCOMPARE(w->windowTitle(), QString("Documents"));
        QCOMPARE(path->text(), QDir::toNativeSeparators(canonical(root.path() + "/Documents")));
        QVERIFY(back->isEnabled() && up->isEnabled() && !forward->isEnabled());
        QTRY_VERIFY(w->directoryState().value("loaded").toBool());
        doubleClick("Letters");
        QTRY_COMPARE(w->directory(), canonical(root.path() + "/Documents/Letters"));
        back->click();
        QCOMPARE(w->directory(), canonical(root.path() + "/Documents"));
        QVERIFY(forward->isEnabled());
        forward->click();
        QCOMPARE(w->directory(), canonical(root.path() + "/Documents/Letters"));
        up->click();
        QCOMPARE(w->directory(), canonical(root.path() + "/Documents"));
        // A typed path, and the Places list.
        path->setText("~nowhere");
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(w->directory(), canonical(root.path() + "/Documents")); // Unknown: stays.
        path->setText(root.path() + "/Documents/Letters");
        QTest::keyClick(path, Qt::Key_Return);
        QCOMPARE(w->directory(), canonical(root.path() + "/Documents/Letters"));
        auto *places = w->findChild<QListWidget *>("placesList");
        QVERIFY(places && places->count() >= 2);
        QCOMPARE(places->item(1)->text(), QString("Desktop"));
        QCOMPARE(places->item(0)->data(Qt::UserRole).toString(), QDir::cleanPath(root.path()));
        emit places->itemClicked(places->item(0));
        QCOMPARE(w->directory(), canonical(root.path()));
        QVERIFY(navigated.count() >= 6);
        // Option-double-click: its own window, as on a Mac.
        QTRY_VERIFY(w->directoryState().value("loaded").toBool());
        doubleClick("Documents", Qt::AltModifier);
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), root.path() + "/Documents");
        QCOMPARE(w->directory(), canonical(root.path()));
    }
    void contextMenusSelectTheClickedItemPreserveGroupsAndOfferBackgroundActions()
    {
        QTemporaryDir root, profile;
        for (const auto &name : {"One.txt", "Two.txt"})
        {
            QFile file(root.path() + '/' + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write(name);
        }
        QPointer<FinderWindow> window = new FinderWindow(root.path(), profile.path(), root.path());
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        auto index = model->index(root.path() + "/One.txt");
        QTRY_VERIFY(icons->visualRect(index).isValid());
        const auto context = [&](QAbstractItemView *view, QPoint point)
        {
            QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                                    view->viewport()->mapToGlobal(point));
            QApplication::sendEvent(view->viewport(), &event);
        };
        context(icons, icons->visualRect(index).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        QPointer<QMenu> menu = window->findChild<QMenu *>("finderContextMenu");
        QVERIFY(menu->isVisible());
        QVERIFY(menu->actions().contains(window->findChild<QAction *>("rename")));
        QVERIFY(window->findChild<QAction *>("duplicate")->isEnabled());
        const auto pixel=[&](const QImage &image,int x,int y) {
            const auto ratio=image.devicePixelRatio();
            return image.pixelColor(int((x+.5)*ratio),int((y+.5)*ratio));
        };
        const auto normal=menu->grab().toImage();
        QCOMPARE(pixel(normal,0,5),QColor(Qt::black));
        QCOMPARE(pixel(normal,1,5),QColor(Qt::white));
        QCOMPARE(pixel(normal,2,5),QColor("#ddd"));
        QCOMPARE(pixel(normal,menu->width()-2,5),QColor("#999"));
        QCOMPARE(pixel(normal,menu->width()-1,5),QColor(Qt::black));
        for(auto *action:menu->actions()) {
            const auto rect=menu->actionGeometry(action);
            QCOMPARE(rect.height(),action->isSeparator()?6:16);
            if(action->isSeparator()) {
                QCOMPARE(pixel(normal,4,rect.top()+2),QColor("#888"));
                QCOMPARE(pixel(normal,4,rect.top()+3),QColor(Qt::white));
            }
        }
        auto *labels=menu->findChild<QMenu *>("finderContextLabels");QVERIFY(labels);
        auto *fileLabels=window->findChild<QMenu *>("labelMenu");QVERIFY(fileLabels);
        QCOMPARE(fileLabels->font().bold(),window->font().bold());
        QCOMPARE(labels->actions(),fileLabels->actions());
        menu->setActiveAction(labels->menuAction());
        QTest::keyClick(menu,Qt::Key_Right);
        QTRY_VERIFY(labels->isVisible());
        QVERIFY(window->findChild<QAction *>("label0")->isChecked());
        auto *red=window->findChild<QAction *>("label2");QVERIFY(red);
        const auto redRect=labels->actionGeometry(red);QCOMPARE(redRect.height(),16);
        const auto beforeLabel=labels->grab().toImage();
        red->trigger();QCOMPARE(FileLabels::label(root.path()+"/One.txt",profile.path()),2);
        QVERIFY(QMetaObject::invokeMethod(labels,"aboutToShow"));QVERIFY(red->isChecked());
        const auto afterLabel=labels->grab().toImage();
        const auto ratio=afterLabel.devicePixelRatio();
        const QRect checkGutter(int(ratio),int(redRect.top()*ratio),int(16*ratio),int(redRect.height()*ratio));
        QVERIFY(beforeLabel.copy(checkGutter)!=afterLabel.copy(checkGutter));
        QTest::keyClick(labels,Qt::Key_Left);
        QTRY_VERIFY(!labels->isVisible());QVERIFY(menu->isVisible());
        auto *duplicate = window->findChild<QAction *>("duplicate");
        menu->setActiveAction(duplicate);
        const auto selected=menu->grab().toImage();
        const auto selectedRect=menu->actionGeometry(duplicate);
        for(int y=selectedRect.top();y<=selectedRect.bottom();++y) {
            QCOMPARE(pixel(selected,1,y),QColor("#6666cc"));
            QCOMPARE(pixel(selected,2,y),QColor("#333399"));
            QCOMPARE(pixel(selected,menu->width()-2,y),QColor("#000088"));
        }
        QVERIFY(menu->grab().save(QCoreApplication::applicationDirPath() +
                                  "/artifacts/finder-context.png"));
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(duplicate).center());
        QTRY_VERIFY(QFileInfo::exists(root.path() + "/One copy.txt"));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        QTRY_VERIFY(menu.isNull());
        // "One copy.txt" sorts before "One.txt": look the item up again once the copy is listed and placed.
        QTRY_VERIFY(icons->visualRect(model->index(root.path() + "/One copy.txt")).isValid());
        index = model->index(root.path() + "/One.txt");
        QTRY_VERIFY(icons->visualRect(index).isValid());
        icons->selectionModel()->select(index, QItemSelectionModel::ClearAndSelect);
        icons->selectionModel()->select(model->index(root.path() + "/Two.txt"),
                                        QItemSelectionModel::Select);
        context(icons, icons->visualRect(index).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QCOMPARE(icons->selectionModel()->selectedIndexes().size(), 2);
        QVERIFY(!window->findChild<QAction *>("rename")->isEnabled());
        QVERIFY(window->findChild<QAction *>("trash")->isEnabled());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        window->setViewMode(true);
        auto *list = window->findChild<FolderList *>("listView");
        QVERIFY(list);
        context(list, list->visualRect(model->index(root.path() + "/Two.txt")).center());
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QSignalSpy opened(window, &FinderWindow::openDocument);
        auto *open = window->findChild<QAction *>("open");
        QTest::mouseClick(menu, Qt::LeftButton, {}, menu->actionGeometry(open).center());
        QTRY_COMPARE(opened.count(), 1);
        QCOMPARE(opened.first().first().toString(), root.path() + "/Two.txt");
        QTRY_VERIFY(menu.isNull());
        context(list, {list->viewport()->width() - 30, list->viewport()->height() - 30});
        QTRY_VERIFY(window->findChild<QMenu *>("finderContextMenu"));
        menu = window->findChild<QMenu *>("finderContextMenu");
        QVERIFY(menu->actions().contains(window->findChild<QAction *>("newFolder")));
        QVERIFY(!menu->actions().contains(window->findChild<QAction *>("trash")));
        QVERIFY(list->selectionModel()->selectedIndexes().isEmpty());
        QTest::keyClick(menu, Qt::Key_Escape);
        QTRY_VERIFY(menu.isNull());
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void fileDropsUseTheActualViewAndPreserveCopySources_data()
    {
        QTest::addColumn<bool>("list");
        QTest::addColumn<bool>("copy");
        QTest::newRow("icons-move") << false << false;
        QTest::newRow("icons-copy") << false << true;
        QTest::newRow("list-move") << true << false;
        QTest::newRow("list-copy") << true << true;
    }
    void fileDropsUseTheActualViewAndPreserveCopySources()
    {
        QFETCH(bool, list);
        QFETCH(bool, copy);
        QTemporaryDir root, profile;
        QVERIFY(QDir(root.path()).mkdir("From"));
        QVERIFY(QDir(root.path()).mkdir("To"));
        const auto source = root.path() + "/From/Dragged.txt",
                   target = root.path() + "/To/Dragged.txt";
        {
            QFile file(source);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("The original bytes.\n");
        }
        QPointer<FinderWindow> window =
            new FinderWindow(root.path(), profile.path(), root.path() + "/To");
        window->setViewMode(list);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        auto *view =
            list ? static_cast<QAbstractItemView *>(window->findChild<FolderList *>("listView"))
                 : static_cast<QAbstractItemView *>(window->findChild<FolderIcons *>("iconView"));
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile(source)});
        mime.setData(finderDragMime, "1");
        const auto modifiers = copy ? Qt::AltModifier : Qt::NoModifier;
        QDragEnterEvent enter(QPointF(60, 60), Qt::CopyAction | Qt::MoveAction, &mime,
                              Qt::LeftButton, modifiers);
        QApplication::sendEvent(view->viewport(), &enter);
        QVERIFY(enter.isAccepted());
        QDropEvent drop({60, 60}, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                        modifiers);
        QApplication::sendEvent(view->viewport(), &drop);
        QVERIFY(drop.isAccepted());
        QCOMPARE(drop.dropAction(), copy ? Qt::CopyAction : Qt::MoveAction);
        QTRY_VERIFY(QFileInfo::exists(target));
        QTRY_VERIFY(!window->directoryState().value("busy").toBool());
        QCOMPARE(QFileInfo::exists(source), copy);
        QFile result(target);
        QVERIFY(result.open(QIODevice::ReadOnly));
        QCOMPARE(result.readAll(), QByteArray("The original bytes.\n"));
        window->close();
        QTRY_VERIFY(window.isNull());
    }
    void fileDragPayloadRejectsMixedRemoteRelativeAndOversizedBatches()
    {
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/A"), QUrl::fromLocalFile("/tmp/A")});
        QCOMPARE(fileDragPaths(&mime), QStringList{"/tmp/A"});
        mime.setUrls({QUrl::fromLocalFile("/tmp/A"), QUrl("https://example.com/A")});
        QVERIFY(fileDragPaths(&mime).isEmpty());
        mime.setUrls({QUrl::fromLocalFile("relative")});
        QVERIFY(fileDragPaths(&mime).isEmpty());
        QList<QUrl> urls;
        for (int n = 0; n < 129; ++n)
            urls.push_back(QUrl::fromLocalFile("/tmp/" + QString::number(n)));
        mime.setUrls(urls);
        QVERIFY(fileDragPaths(&mime).isEmpty());
    }
    void ownedFileDragPayloadPreservesValidationWithoutPortalFallback_data()
    {
        QTest::addColumn<QByteArray>("payload");
        QTest::addColumn<QStringList>("expected");
        const auto valid = QUrl::fromLocalFile(QString::fromUtf8("/tmp/Café #1.txt")).toEncoded();
        QTest::newRow("encoded names and duplicates")
            << valid + "\r\n" + valid + "\r\n"
            << QStringList{QString::fromUtf8("/tmp/Café #1.txt")};
        QTest::newRow("mixed local and remote") << valid + "\r\nhttps://example.com/file\r\n"
                                                << QStringList{};
        QTest::newRow("decoded NUL") << QByteArray("file:///tmp/a%00b\r\n") << QStringList{};
        QTest::newRow("literal NUL") << QByteArray("file:///tmp/a\0b\r\n", 17) << QStringList{};
        QTest::newRow("decoded size limit") << QByteArray(256 * 1024 + 1, 'a') << QStringList{};
        QByteArray many;
        for (int n = 0; n < 129; ++n)
            many += "file:///tmp/" + QByteArray::number(n) + "\r\n";
        QTest::newRow("URL count limit") << many << QStringList{};
        QTest::newRow("empty owned payload") << QByteArray{} << QStringList{};
    }
    void ownedFileDragPayloadPreservesValidationWithoutPortalFallback()
    {
        QFETCH(QByteArray, payload);
        QFETCH(QStringList, expected);
        QMimeData mime;
        mime.setUrls({QUrl::fromLocalFile("/tmp/Fallback.txt")});
        mime.setData(finderDragMime, "transport-token");
        mime.setData(finderFileUrlsMime, payload);
        QCOMPARE(fileDragPaths(&mime), expected);
        // Without the owned-drag marker, an unrelated custom format is ignored.
        mime.removeFormat(finderDragMime);
        QCOMPARE(fileDragPaths(&mime), QStringList{"/tmp/Fallback.txt"});
    }
    void fileOpeningRequestsTheDesktopAndDesktopEntriesStayDocuments()
    {
        QTemporaryDir root, profile;
        const auto path = root.path() + "/An application.desktop";
        QFile file(path);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("[Desktop Entry]\nType=Application\nName=Inert\nExec=/usr/bin/false\n");
        file.close();
        auto *window = new FinderWindow(root.path(), profile.path(), root.path());
        QSignalSpy opened(window, &FinderWindow::openDocument);
        window->show();
        QVERIFY(QTest::qWaitForWindowExposed(window));
        QTRY_VERIFY(window->directoryState().value("loaded").toBool());
        auto *icons = window->findChild<FolderIcons *>("iconView");
        QVERIFY(icons);
        auto *model = qobject_cast<QFileSystemModel *>(icons->model());
        QVERIFY(model);
        icons->selectionModel()->select(model->index(path), QItemSelectionModel::Select);
        QVERIFY(!icons->selectionModel()->selectedIndexes().isEmpty());
        auto *open = window->findChild<QAction *>("open");
        QVERIFY(open);
        open->trigger();
        QCOMPARE(opened.count(), 1);
        QCOMPARE(opened.first().at(0).toString(), path);
        QVERIFY(!opened.first().at(1).toBool());
        auto *with = window->findChild<QAction *>("openWith");
        QVERIFY(with);
        with->trigger();
        QCOMPARE(opened.count(), 2);
        QVERIFY(opened.last().at(1).toBool());
        window->close();
    }
    void editorSaveAsUsesInSessionPickerAndCancelKeepsItsPath()
    {
        QTemporaryDir root;
        EditorWindow window;
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        QVERIFY(text);
        text->setPlainText("A new document saved with the in-session picker.");
        auto *saveAs = window.findChild<QAction *>("saveAs");
        QVERIFY(saveAs);
        const auto path = root.path() + "/New document.txt";
        bool seen = false;
        QTimer responder;
        connect(&responder, &QTimer::timeout, &window,
                [&]
                {
                    auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget());
                    if (!dialog)
                        return;
                    seen = true;
                    QVERIFY(dialog->testOption(QFileDialog::DontUseNativeDialog));
                    auto *name = dialog->findChild<QLineEdit *>("fileNameEdit");
                    auto *buttons = dialog->findChild<QDialogButtonBox *>();
                    if (!name || !buttons)
                        return;
                    if (name->text() != path)
                    {
                        name->setText(path);
                        return;
                    }
                    auto *save = buttons->button(QDialogButtonBox::Save);
                    if (!save || !save->isEnabled())
                        return;
                    responder.stop();
                    QTest::mouseClick(save, Qt::LeftButton);
                });
        QTimer watchdog;
        watchdog.setSingleShot(true);
        watchdog.setInterval(3000);
        connect(
            &watchdog, &QTimer::timeout, &window,
            []
            {
                if (auto *dialog = qobject_cast<QFileDialog *>(QApplication::activeModalWidget()))
                {
                    qWarning() << "Picker did not finish; selection:" << dialog->selectedFiles();
                    dialog->reject();
                }
            });
        responder.start(10);
        watchdog.start();
        saveAs->trigger();
        responder.stop();
        watchdog.stop();
        QVERIFY(seen);
        QFile saved(path);
        QVERIFY(saved.open(QIODevice::ReadOnly));
        QCOMPARE(saved.readAll(), QByteArray("A new document saved with the in-session picker."));
        QVERIFY(window.windowTitle().contains("New document.txt"));
        const auto title = window.windowTitle();
        QTimer::singleShot(0, &window,
                           []
                           {
                               if (auto *dialog =
                                       qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                   dialog->reject();
                           });
        saveAs->trigger();
        QCOMPARE(window.windowTitle(), title);
        window.close();
    }
    void unsupportedTextCannotSilentlyReplaceOriginalBytes_data()
    {
        QTest::addColumn<QByteArray>("bytes");
        QTest::newRow("invalid UTF-8") << QByteArray("text\xff", 5);
        QTest::newRow("incomplete UTF-8") << QByteArray("text\xe2\x82", 6);
        QTest::newRow("embedded NUL") << QByteArray("text\0binary", 11);
    }
    void unsupportedTextCannotSilentlyReplaceOriginalBytes()
    {
        QFETCH(QByteArray, bytes);
        QTemporaryDir root;
        const auto path = root.path() + "/Unsupported.txt";
        {
            QFile file(path);
            QVERIFY(file.open(QIODevice::WriteOnly));
            QCOMPARE(file.write(bytes), bytes.size());
        }
        bool alerted = false;
        QTimer::singleShot(0, this,
                           [&]
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               alerted = true;
                               dialog->accept();
                           });
        EditorWindow window(path);
        QVERIFY(alerted);
        QCOMPARE(window.windowTitle(), QString("untitled"));
        QVERIFY(window.findChild<QTextEdit *>("documentText")->toPlainText().isEmpty());
        QTimer::singleShot(0, &window,
                           []
                           {
                               if (auto *dialog =
                                       qobject_cast<QDialog *>(QApplication::activeModalWidget()))
                                   dialog->reject();
                           });
        QVERIFY(!window.save()); // A rejected load has no writable original path.
        QFile original(path);
        QVERIFY(original.open(QIODevice::ReadOnly));
        QCOMPARE(original.readAll(), bytes);
    }
    void folderViewAndGeometryPersist()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QPointer<FinderWindow> w = new FinderWindow(root.path(), profile.path(), root.path());
        w->show();
        w->resize(580, 360);
        w->setViewMode(true);
        const QSize size = w->size();
        w->close();
        QTRY_VERIFY(w.isNull());
        auto *next = new FinderWindow(root.path(), profile.path(), root.path());
        next->show();
        QCOMPARE(next->size(), size);
        QVERIFY(next->findChild<QTreeView *>("listView")->isVisible());
        next->close();
    }
    void hiddenFilesAreShownInEveryWindowAndRemembered()
    {
        // Lily Hewitt, 2026-10-22: "make it so that you can hide and show hidden files".
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Folder");
        for (const auto *name : {"/.hidden", "/visible", "/Folder/.config", "/Folder/notes"})
        {
            QFile file(root.path() + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
        }
        auto *top = new FinderWindow(root.path(), profile.path(), root.path());
        auto *inner = new FinderWindow(root.path(), profile.path(), root.path() + "/Folder");
        top->show();
        inner->show();
        const auto count = [](FinderWindow *w) { return w->directoryState().value("items").toInt(); };
        QTRY_COMPARE(count(top), 2);   // Folder, visible
        QTRY_COMPARE(count(inner), 1); // notes
        auto *show = top->findChild<QAction *>("showHidden");
        QVERIFY(show && show->isCheckable() && !show->isChecked());
        show->trigger();
        QTRY_COMPARE(count(top), 3);
        QTRY_COMPARE(count(inner), 2);
        QVERIFY(inner->findChild<QAction *>("showHidden")->isChecked());
        QVERIFY(inner->directoryState().value("showHidden").toBool());
        // A new window opens the way the Finder was left.
        auto *again = new FinderWindow(root.path(), profile.path(), root.path() + "/Folder");
        again->show();
        QTRY_COMPARE(count(again), 2);
        QVERIFY(again->findChild<QAction *>("showHidden")->isChecked());
        again->findChild<QAction *>("showHidden")->trigger();
        QTRY_COMPARE(count(top), 2);
        QTRY_COMPARE(count(inner), 1);
        QTRY_COMPARE(count(again), 1);
        for (auto *w : {top, inner, again})
            w->close();
    }
    void repeatedOpenUsesOneFolderWindow()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Documents");
        FinderService finder(root.path(), profile.path());
        QVERIFY(finder.OpenDirectory(root.path() + "/Documents"));
        QVERIFY(finder.OpenDirectory(root.path() + "/Documents/../Documents"));
        QCOMPARE(finder.windowCount(), 1);
        QTRY_VERIFY(finder.DirectoryState(root.path() + "/Documents").value("loaded").toBool());
        QVERIFY(!finder.DirectoryState(root.path() + "/Documents").value("busy").toBool());
        QTemporaryDir outside;
        QVERIFY(!finder.OpenDirectory(outside.path()));
        QVERIFY(finder.DirectoryState(outside.path()).isEmpty());
        QCOMPARE(finder.windowCount(), 1);
        for (auto *window : QApplication::topLevelWidgets())
            if (qobject_cast<FinderWindow *>(window))
                window->close();
        QTRY_COMPARE(finder.windowCount(), 0);
    }
    void newFolderThroughActualDialog()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        auto *w = new FinderWindow(root.path(), profile.path(), root.path());
        w->show();
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QInputDialog *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->setTextValue("Created by GUI");
                               dialog->accept();
                           });
        w->findChild<QAction *>("newFolder")->trigger();
        QTRY_VERIFY(QFileInfo(root.path() + "/Created by GUI").isDir());
        QTest::qWait(100);
        w->close();
    }
    void editAndSaveActualDocument()
    {
        QTemporaryDir root;
        const auto path = root.path() + "/document.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("old");
        f.close();
        EditorWindow window(path);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->setFocus();
        QTest::keyClick(text, Qt::Key_A, Qt::ControlModifier);
        QTest::keyClicks(text, "A native Macintosh experience.");
        QTest::keyClick(text, Qt::Key_S, Qt::ControlModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("A native Macintosh experience."));
        f.close();
        window.close();
    }
    void renderOwnedFinder()
    {
        QTemporaryDir root;
        QTemporaryDir profile;
        QDir(root.path()).mkdir("Documents");
        QDir(root.path()).mkdir("Applications");
        QFile file(root.path() + "/Read Me.txt");
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write("Frankenstein Macintosh\nNative files. Native applications.\n");
        file.close();
        auto *window = new FinderWindow(root.path(), profile.path(), root.path());
        window->setWindowTitle("Hard Disk");
        window->show();
        QTest::qWait(250);
        QDir().mkpath(QCoreApplication::applicationDirPath() + "/artifacts");
        auto image = window->grab().toImage();
        QVERIFY(!image.isNull());
        QVERIFY(image.save(QCoreApplication::applicationDirPath() + "/artifacts/finder.png"));
        window->close();
    }
    void dirtyDocumentCanCancelClose()
    {
        EditorWindow window;
        window.show();
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->insertPlainText("Unsaved work");
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->button(QMessageBox::Cancel)->click();
                           });
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        QCOMPARE(text->toPlainText(), QString("Unsaved work"));
        QTimer::singleShot(0,
                           []
                           {
                               auto *dialog =
                                   qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
                               QVERIFY(dialog);
                               dialog->button(QMessageBox::Discard)->click();
                           });
        QVERIFY(window.close());
    }
    void returnInSaveChangesSavesEvenWithDiscardFocused()
    {
        // Return is the ringed default, Save, even when Discard has focus: it never throws work away.
        QTemporaryDir dir;
        const auto path = dir.path() + "/story.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("before");
        f.close();
        EditorWindow window(path);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->selectAll();
        text->insertPlainText("after");
        bool returnWasDelivered = false, saveWasDefault = false, timedOut = false;
        QTimer watchdog;
        watchdog.setSingleShot(true);
        auto *poll = new QTimer(&window);
        poll->setInterval(20);
        connect(poll, &QTimer::timeout, poll, [poll, &returnWasDelivered, &saveWasDefault]
        {
            auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget());
            if (!box)
                return;
            poll->stop();
            saveWasDefault = box->defaultButton() == box->button(QMessageBox::Save);
            auto *discard = box->button(QMessageBox::Discard);
            if (!saveWasDefault || !discard)
            {
                box->reject(); // Report below, after exec() exits; no assertion strands the modal.
                return;
            }
            discard->setFocus(Qt::TabFocusReason);
            returnWasDelivered = discard->hasFocus();
            QTest::keyClick(discard, Qt::Key_Return);
        });
        connect(&watchdog, &QTimer::timeout, &window, [&]
        {
            timedOut = true;
            poll->stop();
            if (auto *box = qobject_cast<QMessageBox *>(QApplication::activeModalWidget()))
                box->reject();
        });
        poll->start();
        watchdog.start(5000);
        const bool closed = window.close();
        watchdog.stop();
        poll->stop();
        QVERIFY2(!timedOut, "Save Changes test did not finish its modal question within five seconds");
        QVERIFY(saveWasDefault);
        QVERIFY(closed);
        QVERIFY(returnWasDelivered);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("after"));
    }
    void commandShortcutSavesDocument()
    {
        QTemporaryDir dir;
        const auto path = dir.path() + "/native.txt";
        QFile f(path);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write("before");
        f.close();
        EditorWindow window(path);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        auto *text = window.findChild<QTextEdit *>("documentText");
        text->selectAll();
        text->insertPlainText("after");
        text->setFocus();
        QTest::keyClick(text, Qt::Key_S, Qt::MetaModifier);
        QTRY_VERIFY(!text->document()->isModified());
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("after"));
        window.close();
    }
};
QTEST_MAIN(GuiTests)
#include "gui_tests.moc"
