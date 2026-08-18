#include "mainwindow.h"
#include <QApplication>
#include <QTranslator>
#include <QToolBar>
#include <QLineEdit>
#include <QComboBox>
#include <QLabel>
#include <QTableView>
#include <QPushButton>
#include <QGroupBox>
#include <QTextEdit>
#include <QTimer>

// some stuff from stackoverflow
void myMessageOutput(QtMsgType type, const QMessageLogContext& context, const QString& msg)
{
    if(MainWindow::log == nullptr) {
        QByteArray localMsg = msg.toLocal8Bit();
        switch (type) {
            case QtDebugMsg:
                fprintf(stderr, "Debug: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
                break;
            case QtWarningMsg:
                fprintf(stderr, "Warning: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
                break;
            case QtCriticalMsg:
                fprintf(stderr, "Critical: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
                break;
            case QtInfoMsg:
                fprintf(stderr, "Info: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
                break;
            case QtFatalMsg:
                fprintf(stderr, "Fatal: %s (%s:%u, %s)\n", localMsg.constData(), context.file, context.line, context.function);
                abort();
        }
    }
    else {
        switch (type) {
            case QtDebugMsg:
            case QtWarningMsg:
            case QtCriticalMsg:
            case QtInfoMsg:
                if(MainWindow::log != nullptr) {
                    MainWindow::log->append(msg);
                }
                break;
            case QtFatalMsg:
                abort();
        }
    }
}

int main(int argc, char* argv[])
{
    qInstallMessageHandler(myMessageOutput);
    QApplication a(argc, argv);
    QTranslator myTranslator;

    if ( QLocale::system().name().contains("ru_RU") ) {
        myTranslator.load("W3ModMerger_ru_RU", ":/translations");
    }
    a.installTranslator(&myTranslator);

    MainWindow w;
    w.setWindowTitle("W3ModMerger-NG");

    // 2026 workspace controls. Keep the existing merge engine intact while
    // making large mod lists navigable and the old button names explicit.
    QToolBar* workspaceBar = new QToolBar(QObject::tr("Workspace"), &w);
    workspaceBar->setMovable(false);
    workspaceBar->setFloatable(false);
    w.addToolBar(Qt::TopToolBarArea, workspaceBar);

    QLabel* viewLabel = new QLabel(QObject::tr("View:"), workspaceBar);
    QComboBox* viewCombo = new QComboBox(workspaceBar);
    viewCombo->addItem(QObject::tr("All Mods"));
    viewCombo->addItem(QObject::tr("Available / Unmerged"));
    viewCombo->addItem(QObject::tr("Verified NG Sources"));
    viewCombo->addItem(QObject::tr("Merged Sources (Experimental)"));
    viewCombo->addItem(QObject::tr("ModPacks"));
    viewCombo->setToolTip(QObject::tr("Verified NG Sources are proven by an embedded NG manifest. Experimental merged sources are legacy/unknown relationships inferred only from their merged state."));

    QLineEdit* searchEdit = new QLineEdit(workspaceBar);
    searchEdit->setPlaceholderText(QObject::tr("Search mods or ModPacks..."));
    searchEdit->setClearButtonEnabled(true);
    searchEdit->setMinimumWidth(300);
    searchEdit->setToolTip(QObject::tr("Search mod names, status, and notes. Results update as you type."));

    workspaceBar->addWidget(viewLabel);
    workspaceBar->addWidget(viewCombo);
    workspaceBar->addSeparator();
    workspaceBar->addWidget(searchEdit);

    QTableView* table = w.findChild<QTableView*>("tableView");
    auto applyFilters = [table, searchEdit, viewCombo]() {
        if (!table || !table->model()) {
            return;
        }

        const QString needle = searchEdit->text().trimmed();
        const int view = viewCombo->currentIndex();

        for (int row = 0; row < table->model()->rowCount(); ++row) {
            const QString name = table->model()->data(table->model()->index(row, Columns::MOD_NAME)).toString();
            const QString status = table->model()->data(table->model()->index(row, Columns::STATUS)).toString();
            const QString notes = table->model()->data(table->model()->index(row, Columns::NOTES_LAST)).toString();

            const bool searchMatch = needle.isEmpty() ||
                    name.contains(needle, Qt::CaseInsensitive) ||
                    status.contains(needle, Qt::CaseInsensitive) ||
                    notes.contains(needle, Qt::CaseInsensitive);

            bool viewMatch = true;
            if (view == 1) {
                viewMatch = status.compare(QObject::tr("Not merged"), Qt::CaseInsensitive) == 0;
            }
            else if (view == 2) {
                viewMatch = status.compare(QObject::tr("Verified NG Source"), Qt::CaseInsensitive) == 0;
            }
            else if (view == 3) {
                viewMatch = status.compare(QObject::tr("Merged Source (Experimental)"), Qt::CaseInsensitive) == 0;
            }
            else if (view == 4) {
                viewMatch = status.contains(QObject::tr("pack"), Qt::CaseInsensitive);
            }

            table->setRowHidden(row, !(searchMatch && viewMatch));
        }
    };

    QObject::connect(searchEdit, &QLineEdit::textChanged, &w, [applyFilters](const QString&) {
        applyFilters();
    });
    QObject::connect(viewCombo, static_cast<void(QComboBox::*)(int)>(&QComboBox::currentIndexChanged),
                     &w, [applyFilters](int) { applyFilters(); });
    QObject::connect(&w, &MainWindow::modsListChanged, &w, [applyFilters]() {
        QTimer::singleShot(0, applyFilters);
    });

    if (QLineEdit* modsPath = w.findChild<QLineEdit*>("lineEditModsPath")) {
        QObject::connect(modsPath, &QLineEdit::textChanged, &w, [applyFilters](const QString&) {
            QTimer::singleShot(0, applyFilters);
        });
    }

    if (QGroupBox* modsFolderBox = w.findChild<QGroupBox*>("groupBox")) {
        modsFolderBox->setTitle(QObject::tr("Witcher 3 Mods Folder"));
        modsFolderBox->setToolTip(QObject::tr("The game's deployed Mods directory. W3ModMerger-NG scans this folder for mergeable mods and ModPacks."));
    }

    auto setButton = [&w](const char* objectName, const QString& text, const QString& tooltip) {
        if (QPushButton* button = w.findChild<QPushButton*>(objectName)) {
            button->setText(text);
            button->setToolTip(tooltip);
        }
    };

    setButton("buttonUp", QObject::tr("↑ Priority"), QObject::tr("Move the selected mod earlier in the merge order. Merge order is priority-sensitive."));
    setButton("buttonDown", QObject::tr("↓ Priority"), QObject::tr("Move the selected mod later in the merge order. Merge order is priority-sensitive."));
    setButton("buttonRecommended", QObject::tr("Suggest Mods"), QObject::tr("Selects likely merge candidates. This is not a one-click solution; review the selection and conflicts before creating a ModPack."));
    setButton("buttonSelectAll", QObject::tr("Select All"), QObject::tr("Select every available/unmerged mod in the current workspace model."));
    setButton("buttonDeselect", QObject::tr("Clear Selection"), QObject::tr("Clear all selected mods."));
    setButton("buttonConflicts", QObject::tr("Review Conflicts"), QObject::tr("Show resources that are present in more than one selected mod."));
    setButton("buttonMerge", QObject::tr("Create ModPack"), QObject::tr("Create a ModPack from the selected mods using the configured ModKit pipeline."));
    setButton("buttonUnmerge", QObject::tr("Restore Sources"), QObject::tr("Restore source mods that were disabled by a previous merge. NG packs are removed only when their embedded manifest proves ownership."));

    if (QTextEdit* detailedLog = w.findChild<QTextEdit*>("textEditLog")) {
        detailedLog->setMaximumHeight(150);
        detailedLog->setPlaceholderText(QObject::tr("Detailed WCC / pipeline output appears here. This log is primarily for diagnostics."));
        detailedLog->setToolTip(QObject::tr("Raw pipeline output. Normal workflow status will move to structured UI as the NG overhaul progresses."));
    }

    w.show();

    return a.exec();
}
