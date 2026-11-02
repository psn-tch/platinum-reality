#include "appearance/interface_font.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPainter>
#include <QPushButton>
#include <QStyleOptionSlider>
#include <QRadioButton>
#include <QScrollBar>
#include <QStyle>
#include <QStyleFactory>
#include <QVBoxLayout>
#include <QtTest>
class StylePluginTests : public QObject
{
    Q_OBJECT
  private slots:
    void classicControlsStillRespondToRealPointerAndKeyboardInput()
    {
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *label = new QLabel("Platinum Reality — ordinary Qt controls", &window);
        auto *edit = new QLineEdit(&window);
        auto *check = new QCheckBox("Keep modern functionality", &window);
        auto *radio = new QRadioButton("Classic appearance", &window);
        auto *scroll = new QScrollBar(Qt::Horizontal, &window);
        scroll->setRange(0, 100);
        scroll->setPageStep(20);
        auto *vertical = new QScrollBar(Qt::Vertical, &window);
        vertical->setRange(0, 100);
        layout->addWidget(label);
        layout->addWidget(edit);
        layout->addWidget(check);
        layout->addWidget(radio);
        layout->addWidget(scroll);
        layout->addWidget(vertical);
        auto *buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
        layout->addWidget(buttons);
        window.resize(380, 300);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        QTest::mouseClick(edit, Qt::LeftButton);
        QTest::keyClicks(edit, "Native app, classic controls");
        QCOMPARE(edit->text(), QString("Native app, classic controls"));
        QTest::mouseClick(check, Qt::LeftButton, {}, {6, check->height() / 2});
        QVERIFY(check->isChecked());
        QTest::mouseClick(radio, Qt::LeftButton, {}, {6, radio->height() / 2});
        QVERIFY(radio->isChecked());
        QTest::mouseClick(scroll, Qt::LeftButton, {}, {scroll->width() - 5, scroll->height() / 2});
        QVERIFY(scroll->value() > 0);
        QTest::mouseClick(vertical, Qt::LeftButton, {},
                          {vertical->width() / 2, vertical->height() - 5});
        QVERIFY(vertical->value() > 0);
        const auto scrollImage = scroll->grab();
        const auto sample =
            QPointF(scroll->width() - 4, scroll->height() / 2) * scrollImage.devicePixelRatio();
        QCOMPARE(scrollImage.toImage().pixelColor(sample.toPoint()), QColor("#dddddd"));
        const auto directory = QCoreApplication::applicationDirPath() + "/artifacts";
        QDir().mkpath(directory);
        QVERIFY(window.grab().save(directory + "/qt-application-style.png"));
    }
    void scrollBarsAreDrawnAsInMacOS922()
    {
        // Measured in a 9.2.2 Finder window with Smart Scrolling (psn_tch's setting, 2026-10-23): a
        // 16 x 184 bar whose scroll box covers 114 of its 154 travel rows, at the top.
        std::unique_ptr<QStyle> style(QStyleFactory::create("Frankenstein"));
        QVERIFY(style);
        const auto render = [&](QStyle::State state, int maximum) {
            QStyleOptionSlider option;
            option.rect = QRect(0, 0, 16, 184);
            option.orientation = Qt::Vertical;
            option.minimum = 0;
            option.maximum = maximum;
            option.pageStep = 100;
            option.sliderPosition = option.sliderValue = 0;
            option.subControls = QStyle::SC_All;
            option.state = state;
            QImage image(16, 184, QImage::Format_RGB32);
            image.fill(Qt::magenta);
            QPainter painter(&image);
            style->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter);
            painter.end();
            return image;
        };
        const auto at = [](const QImage &image, int x, int y) { return image.pixelColor(x, y).name().mid(1); };
        auto active = render(QStyle::State_Enabled | QStyle::State_Active, 35);
        active.save(QCoreApplication::applicationDirPath() + "/artifacts/scrollbar-active.png");
        // Edges, the scroll box (rows 0-113) with its grip, the track's shadow and inset, two arrows.
        QCOMPARE(at(active, 0, 50), QString("000000"));
        QCOMPARE(at(active, 15, 50), QString("000000"));
        QCOMPARE(at(active, 8, 0), QString("000000"));
        QCOMPARE(at(active, 1, 1), QString("eeeeee"));
        QCOMPARE(at(active, 5, 1), QString("ccccff"));
        QCOMPARE(at(active, 8, 23), QString("9999ff"));
        QCOMPARE(at(active, 14, 23), QString("6666cc"));
        QCOMPARE(at(active, 8, 113), QString("000000"));
        QCOMPARE(at(active, 8, 112), QString("6666cc"));
        QCOMPARE(at(active, 8, 114), QString("777777"));
        QCOMPARE(at(active, 8, 115), QString("888888"));
        QCOMPARE(at(active, 8, 130), QString("aaaaaa"));
        QCOMPARE(at(active, 1, 130), QString("777777"));
        QCOMPARE(at(active, 2, 130), QString("888888"));
        QCOMPARE(at(active, 13, 130), QString("bbbbbb"));
        QCOMPARE(at(active, 14, 130), QString("cccccc"));
        QCOMPARE(at(active, 8, 153), QString("000000"));
        QCOMPARE(at(active, 8, 168), QString("000000"));
        QCOMPARE(at(active, 8, 183), QString("000000"));
        QCOMPARE(at(active, 1, 154), QString("ffffff"));
        QCOMPARE(at(active, 8, 159), QString("000000")); // the up arrow's tip (rows 159-162)
        QCOMPARE(at(active, 6, 159), QString("dddddd"));
        QCOMPARE(at(active, 4, 162), QString("000000"));
        QCOMPARE(at(active, 14, 160), QString("bbbbbb"));
        // Nothing to scroll: a flat track and gray arrows. An inactive window: an empty strip.
        const auto idle = render(QStyle::State_Enabled | QStyle::State_Active, 0);
        QCOMPARE(at(idle, 8, 60), QString("eeeeee"));
        QCOMPARE(at(idle, 8, 159), QString("888888"));
        {
            // The same Finder window's idle horizontal bar: 391 x 16, nothing to scroll.
            QStyleOptionSlider option;
            option.rect = QRect(0, 0, 391, 16);
            option.orientation = Qt::Horizontal;
            option.maximum = 0;
            option.subControls = QStyle::SC_All;
            option.state = QStyle::State_Enabled | QStyle::State_Active;
            QImage image(391, 16, QImage::Format_RGB32);
            image.fill(Qt::magenta);
            QPainter painter(&image);
            style->drawComplexControl(QStyle::CC_ScrollBar, &option, &painter);
            painter.end();
            image.save(QCoreApplication::applicationDirPath() + "/artifacts/scrollbar-idle-horizontal.png");
            QCOMPARE(at(image, 200, 8), QString("eeeeee"));
            QCOMPARE(at(image, 360, 8), QString("555555"));
            QCOMPARE(at(image, 375, 8), QString("888888"));
        }
        const auto inactive = render(QStyle::State_Enabled, 35);
        inactive.save(QCoreApplication::applicationDirPath() + "/artifacts/scrollbar-inactive.png");
        QCOMPARE(at(inactive, 8, 60), QString("eeeeee"));
        QCOMPARE(at(inactive, 8, 159), QString("eeeeee"));
        QCOMPARE(at(inactive, 0, 60), QString("555555"));
    }
    void theDefaultButtonHasMacOS9sRing()
    {
        // Mac OS 9 draws a thick rounded ring around the button Return presses, outside its bevel;
        // the other buttons get the same room, so a row of buttons stays lined up.
        QDialog dialog;
        auto *layout = new QHBoxLayout(&dialog);
        auto *cancel = new QPushButton("Cancel", &dialog);
        auto *ok = new QPushButton("OK", &dialog);
        layout->addWidget(cancel);
        layout->addWidget(ok);
        ok->setDefault(true);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QCOMPARE(qApp->style()->pixelMetric(QStyle::PM_ButtonDefaultIndicator), 4);
        QCOMPARE(ok->height(), cancel->height());
        const auto picture = dialog.grab().toImage();
        const auto at = [&](QPushButton *button, int x) {
            return QColor(picture.pixel(button->x() + x, button->y() + button->height() / 2)).name().mid(1);
        };
        QCOMPARE(at(ok, 1), QString("222222"));     // The ring,
        QCOMPARE(at(ok, 3), QString("dddddd"));     // a gap,
        QCOMPARE(at(ok, 4), QString("222222"));     // then the button's own outline.
        QCOMPARE(at(cancel, 1), QString("dddddd")); // Not the default: no ring,
        QCOMPARE(at(cancel, 4), QString("222222")); // and its outline lines up with OK's.
        // A window that is not a dialog (the installer): its default button keeps the ring after
        // focus moves on, and gets its own room around the bevel.
        QWidget window;
        auto *row = new QHBoxLayout(&window);
        auto *back = new QPushButton("Go Back", &window);
        auto *next = new QPushButton("Continue", &window);
        auto *field = new QLineEdit(&window);
        row->addWidget(back);
        row->addWidget(next);
        row->addWidget(field);
        next->setDefault(true);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        next->setFocus();
        field->setFocus();
        QCOMPARE(next->height(), back->height() + 8);
        const auto plain = window.grab().toImage();
        const auto inWindow = [&](QPushButton *button, int x) {
            return QColor(plain.pixel(button->x() + x, button->y() + button->height() / 2)).name().mid(1);
        };
        QCOMPARE(inWindow(next, 1), QString("222222"));
        QCOMPARE(inWindow(next, 3), QString("dddddd"));
        QCOMPARE(inWindow(next, 4), QString("222222"));
        QCOMPARE(inWindow(back, 0), QString("222222"));
    }
    void theTextFieldWithTheKeyboardHasAFocusRing()
    {
        // Mac OS 9 rings the text field that has the keyboard; the room is kept, so nothing moves.
        QWidget window;
        auto *layout = new QVBoxLayout(&window);
        auto *first = new QLineEdit("first", &window);
        auto *second = new QLineEdit("second", &window);
        layout->addWidget(first);
        layout->addWidget(second);
        window.show();
        QVERIFY(QTest::qWaitForWindowExposed(&window));
        first->setFocus();
        QTRY_VERIFY(first->hasFocus());
        const auto height = first->height();
        const auto at = [&](QLineEdit *field, int x) {
            const auto picture = window.grab().toImage();
            return QColor(picture.pixel(field->x() + x, field->y() + field->height() / 2)).name().mid(1);
        };
        QCOMPARE(at(first, 0), QString("666699"));  // The ring,
        QCOMPARE(at(first, 1), QString("666699"));
        QCOMPARE(at(first, 2), QString("222222"));  // then the field's own outline.
        QCOMPARE(at(second, 0), QString("dddddd")); // No ring without the keyboard,
        QCOMPARE(at(second, 2), QString("222222")); // and the outline in the same place.
        second->setFocus();
        QTRY_VERIFY(second->hasFocus());
        QCOMPARE(at(first, 0), QString("dddddd"));
        QCOMPARE(at(second, 0), QString("666699"));
        QCOMPARE(first->height(), height);
    }
    void ordinaryQtApplicationLoadsBuildLocalStyle()
    {
        QVERIFY(QStyleFactory::keys().contains("Frankenstein"));
        QCOMPARE(qApp->style()->objectName(), QString("frankenstein"));
        QCOMPARE(qApp->font().family(), classicInterfaceFontFamily());
        QWidget dialog;
        auto *layout = new QVBoxLayout(&dialog);
        layout->addWidget(new QLabel("An ordinary Qt application", &dialog));
        layout->addWidget(new QLineEdit("Platinum controls", &dialog));
        auto *buttons =
            new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        layout->addWidget(buttons);
        dialog.show();
        QVERIFY(QTest::qWaitForWindowExposed(&dialog));
        QVERIFY(!dialog.grab().isNull());
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->icon().isNull());
        QVERIFY(buttons->button(QDialogButtonBox::Ok)->x() >
                buttons->button(QDialogButtonBox::Cancel)->x());
        QCOMPARE(qApp->palette().color(QPalette::Window), QColor("#dddddd"));
    }
};
QTEST_MAIN(StylePluginTests)
#include "style_plugin_tests.moc"
