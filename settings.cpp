#include "settings.h"
#include "ui_settings.h"

#include <QDir>
#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

Settings::Settings(QWidget* parent) :
    QDialog(parent),
    ui(new Ui::Settings)
{
    ui->setupUi(this);

    setWindowTitle(tr("W3ModMerger-NG Settings"));

    ui->groupBoxWcc->setTitle(tr("Path to wcc_lite.exe"));
    ui->groupBoxWcc->setToolTip(tr("Select wcc_lite.exe from the Witcher 3 ModKit. W3ModMerger-NG uses it to uncook, cook, build caches, pack files, and generate metadata."));
    ui->lineEditWcc->setToolTip(ui->groupBoxWcc->toolTip());

    ui->groupBoxModName->setTitle(tr("ModPack Name"));
    ui->groupBoxModName->setToolTip(tr("Folder/name to use for the next ModPack created by the merge pipeline."));
    ui->lineEditModName->setToolTip(ui->groupBoxModName->toolTip());

    ui->groupBoxFolders->setTitle(tr("Working Folders"));
    ui->lineEditUncooked->setToolTip(tr("Temporary extracted files created during the uncook stage."));
    ui->lineEditCooked->setToolTip(tr("Temporary processed files created during the cook stage."));
    ui->lineEditPacked->setToolTip(tr("Destination folder where completed ModPacks are built."));

    ui->groupBoxMisc->setTitle(tr("Workflow Options"));
    ui->checkBoxAutoInstall->setText(tr("Auto-install ModPack (not recommended with Vortex)"));
    ui->checkBoxAutoInstall->setToolTip(tr("Copies the completed ModPack directly into the game's Mods folder. If Vortex manages your mods, manual import/deployment is usually safer."));

    ui->checkBoxCleanDirs->setText(tr("Automatically delete working folders after completion"));
    ui->checkBoxCleanDirs->setToolTip(tr("Deletes temporary Uncooked/Cooked working data after a successful merge."));

    ui->checkBoxMergingOrder->setText(tr("Remember Merge Order (priority-sensitive)"));
    ui->checkBoxMergingOrder->setToolTip(tr("Restores the previous merge order on the next scan. Order matters when selected mods contain the same resource."));

    ui->checkBoxShowPause->setText(tr("Pause for manual file cleanup (Advanced)"));
    ui->checkBoxShowPause->setToolTip(tr("Pauses after pipeline stages so advanced users can manually inspect/remove files from the Uncooked/Cooked working folders before continuing."));

    ui->groupBoxAdvanced->setTitle(tr("Advanced / Developer Options — USE AT OWN RISK"));
    ui->groupBoxAdvanced->setToolTip(tr("Experimental pipeline controls. Changing WCC commands or flags can produce incomplete or unusable ModPacks. Leave these at their defaults unless you understand the Witcher 3 ModKit pipeline."));
    ui->lineCmdUncook->setToolTip(ui->groupBoxAdvanced->toolTip());
    ui->lineCmdCook->setToolTip(ui->groupBoxAdvanced->toolTip());
    ui->lineCmdCache->setToolTip(ui->groupBoxAdvanced->toolTip());
    ui->lineCmdPack->setToolTip(ui->groupBoxAdvanced->toolTip());
    ui->lineCmdMetadata->setToolTip(ui->groupBoxAdvanced->toolTip());

    QPushButton* quickGuide = new QPushButton(tr("Quick Guide"), this);
    quickGuide->setToolTip(tr("Explain the working folders, merge order, Suggested Mods, and conflict review."));
    ui->verticalLayoutButtons->insertWidget(3, quickGuide);

    connect(quickGuide, &QPushButton::clicked, this, [=]() {
        QMessageBox guide(this);
        guide.setWindowTitle(tr("W3ModMerger-NG Quick Guide"));
        guide.setIcon(QMessageBox::Information);
        guide.setTextFormat(Qt::RichText);
        guide.setText(tr(
            "<b>Uncooked</b><br>Temporary files extracted by wcc_lite for processing.<br><br>"
            "<b>Cooked</b><br>Temporary processed files prepared for cache building and packing.<br><br>"
            "<b>Packed</b><br>Destination where completed ModPacks are created.<br><br>"
            "<b>Merge Order</b><br>Priority-sensitive. When multiple selected mods contain the same resource, their order can affect the final result.<br><br>"
            "<b>Suggest Mods</b><br>This is a quick candidate selection tool, <b>not a one-click merge solution</b>. Review the selection and decide what you actually want to combine.<br><br>"
            "<b>Review Conflicts</b><br>Shows resources shared by multiple selected mods. Inspect these before creating the ModPack."
        ));
        guide.exec();
    });
}

Settings::~Settings()
{
    delete ui;
}

void Settings::fromWindowToVars()
{
    pathWcc             = ui->lineEditWcc->text();
    pathUncooked        = ui->lineEditUncooked->text();
    pathCooked          = ui->lineEditCooked->text();
    pathPacked          = ui->lineEditPacked->text();
    mergedModName       = ui->lineEditModName->text();

    cmdUncook           = ui->lineCmdUncook->text();
    cmdCook             = ui->lineCmdCook->text();
    cmdCache            = ui->lineCmdCache->text();
    cmdPack             = ui->lineCmdPack->text();
    cmdMetadata         = ui->lineCmdMetadata->text();

    isWccSpecified      = pathWcc.endsWith(Constants::EXE_NAME, Qt::CaseInsensitive);
    autoInstallEnabled  = ui->checkBoxAutoInstall->isChecked();
    autoCleanEnabled    = ui->checkBoxCleanDirs->isChecked();
    skipErrors          = ui->checkBoxSkipErrors->isChecked();
    dumpSwf             = ui->checkBoxSwf->isChecked();
    saveMergingOrder    = ui->checkBoxMergingOrder->isChecked();
    showPauseMessage    = ui->checkBoxShowPause->isChecked();
}

void Settings::fromVarsToWindow()
{
    ui->lineEditWcc->setText(pathWcc);
    ui->lineEditUncooked->setText(pathUncooked);
    ui->lineEditCooked->setText(pathCooked);
    ui->lineEditPacked->setText(pathPacked);
    ui->lineEditModName->setText(mergedModName);

    ui->lineCmdUncook->setText(cmdUncook);
    ui->lineCmdCook->setText(cmdCook);
    ui->lineCmdCache->setText(cmdCache);
    ui->lineCmdPack->setText(cmdPack);
    ui->lineCmdMetadata->setText(cmdMetadata);

    ui->checkBoxAutoInstall->setChecked(autoInstallEnabled);
    ui->checkBoxCleanDirs->setChecked(autoCleanEnabled);
    ui->checkBoxSkipErrors->setChecked(skipErrors);
    ui->checkBoxSwf->setChecked(dumpSwf);
    ui->checkBoxMergingOrder->setChecked(saveMergingOrder);
    ui->checkBoxShowPause->setChecked(showPauseMessage);

    // Some additional checks
    if ( pathWcc.contains(' ') ) {
        toLog( tr("WARNING: path to wcc_lite.exe contains spaces. This may cause problems with wcc.", "Log warning message.") );
        ui->lineEditWcc->setStyleSheet("QLineEdit { background: rgb(255, 0, 0); }");
    }
    else  {
        ui->lineEditWcc->setStyleSheet("QLineEdit { background: rgb(0, 255, 0); }");
    }

    checkFolderLength("Uncooked", pathUncooked, ui->lineEditUncooked);
    checkFolderLength("Cooked", pathCooked, ui->lineEditCooked);
    checkFolderLength("Packed", pathPacked, ui->lineEditPacked);
}

QString Settings::currentDir() const
{
    QDir dir;
    return QDir::toNativeSeparators( dir.absolutePath() );
}

QString Settings::rebuildCmd()
{
    QString result = Constants::DEFAULT_UNCOOK;
    result.remove(" -skiperrors -dumpswf");

    if (skipErrors) {
        result += " -skiperrors";
    }

    if (dumpSwf) {
        result += " -dumpswf";
    }

    return result;
}

void Settings::checkFolderLength(const QString& name, const QString& path, QLineEdit* widget)
{
    if ( path.length() > Constants::PATH_LENGTH_LIMIT ) {
        toLog( tr( "WARNING: path to %1 folder exceeds %2 symbols. This may cause problems with wcc.", "Log warning message.").arg(name).arg(QString::number(Constants::PATH_LENGTH_LIMIT)) );
        widget->setStyleSheet("QLineEdit { background: rgb(255, 0, 0); }");
    }
    else  {
        widget->setStyleSheet("QLineEdit { background: rgb(0, 255, 0); }");
    }
}

void Settings::on_buttonReset_clicked()
{
    using namespace Constants;

    //pathWcc           = "";
    pathUncooked        = currentDir() + DIR_UNCOOKED;
    pathCooked          = currentDir() + DIR_COOKED;
    pathPacked          = currentDir() + DIR_PACKED;
    mergedModName		= DEFAULT_NAME;

    cmdUncook           = DEFAULT_UNCOOK;
    cmdCook             = DEFAULT_COOK;
    cmdCache            = DEFAULT_CACHE;
    cmdPack             = DEFAULT_PACK;
    cmdMetadata         = DEFAULT_META;

    isWccSpecified      = pathWcc.endsWith(EXE_NAME, Qt::CaseInsensitive);
    autoInstallEnabled  = true;
    autoCleanEnabled    = true;
    skipErrors          = true;
    dumpSwf             = true;
    saveMergingOrder    = true;
    showPauseMessage    = false;

    mergingOrder.clear();
    hiddenMods.clear();

    fromVarsToWindow();
}


void Settings::on_buttonWcc_clicked()
{
    QString directory = QFileDialog::getOpenFileName( this, tr("wcc_lite.exe location:", "File selection dialog title."), QDir::currentPath(), Constants::EXE_NAME );

    if ( !directory.isEmpty() ) {
        pathWcc = QDir::toNativeSeparators(directory);
        ui->lineEditWcc->setText(pathWcc);
    }
}

void Settings::on_buttonUncooked_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Uncooked folder path:", "Folder selection dialog title."), QDir::currentPath() );

    if ( !directory.isEmpty() ) {
        pathUncooked = QDir::toNativeSeparators(directory);
        ui->lineEditUncooked->setText(pathUncooked);
    }
}


void Settings::on_buttonCooked_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Cooked folder path:", "Folder selection dialog title."), QDir::currentPath() );

    if ( !directory.isEmpty() ) {
        pathCooked = QDir::toNativeSeparators(directory);
        ui->lineEditCooked->setText(pathCooked);
    }
}
void Settings::on_buttonPacked_clicked()
{
    QString directory = QFileDialog::getExistingDirectory(this, tr("Packed folder path:", "Folder selection dialog title."), QDir::currentPath() );

    if ( !directory.isEmpty() ) {
        pathPacked = QDir::toNativeSeparators(directory);
        ui->lineEditPacked->setText(pathPacked);
    }
}

void Settings::on_lineEditWcc_textChanged(const QString& arg1)
{
    pathWcc = arg1;
    isWccSpecified = pathWcc.endsWith(Constants::EXE_NAME, Qt::CaseInsensitive);
    ui->buttonOk->setEnabled( isWccSpecified );

    if ( isWccSpecified ) {
        ui->lineEditWcc->setStyleSheet("QLineEdit { background: rgb(0, 255, 0); }");
    }
    else {
        ui->lineEditWcc->setStyleSheet("QLineEdit { background: rgb(255, 0, 0); }");
    }
}

void Settings::on_checkBoxSkipErrors_clicked()
{
    skipErrors = ui->checkBoxSkipErrors->isChecked();
    cmdUncook = rebuildCmd();
    fromVarsToWindow();
}

void Settings::on_checkBoxSwf_clicked()
{
    dumpSwf = ui->checkBoxSwf->isChecked();
    cmdUncook = rebuildCmd();
    fromVarsToWindow();
}
