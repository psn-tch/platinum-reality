#include "appearance/interface_font.hpp"
#include <QApplication>
#include <QCheckBox>
#include <QDialogButtonBox>
#include <QDir>
#include <QLabel>
#include <QLineEdit>
#include <QPushButton>
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
