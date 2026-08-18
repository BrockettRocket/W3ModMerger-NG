#include "merger.h"
#include "unpacker.h"
#include "pausemessagebox.h"
#include "metadatastore.h"
#include "singlemod.h"

#include <QDirIterator>
#include <QFileInfo>

Merger::Merger(const QList<Mod*>& mods, const Settings* s, QObject* parent)
    : QObject(parent), modList(mods), settings(s)
{
    shouldPause = settings->showPauseMessage;

    wcc = new QProcess(this);
    QString workingDir = settings->pathWcc;
    wcc->setWorkingDirectory(workingDir.remove("wcc_lite.exe"));

    // Merger slots
    connect(this, &Merger::mergingStarted, this, &Merger::prepare);
    connect(this, &Merger::startImagesDeleting, this, &Merger::deleteImages);
    connect(this, &Merger::startCooking, this, &Merger::cookAll);
    connect(this, &Merger::skipCooking, this, &Merger::unpackAll);

    // Process slots
    connect(wcc, &QProcess::readyReadStandardOutput, this, &Merger::processOutput);
    connect(wcc, &QProcess::readyReadStandardError, this, &Merger::processOutput);

    connect(wcc, &QProcess::errorOccurred,
        [=](QProcess::ProcessError error) {
            processErrored = (error != QProcess::UnknownError);
            QString message = "Process error: " + QString::number(error);
            processBuffer.append(message + "\n");
            emit toLog(message);

            // FailedToStart does not reliably emit finished(), so abort here.
            if (isRunning && error == QProcess::FailedToStart) {
                abortMerge(tr("wcc_lite.exe failed to start."));
            }
        }
    );
}

Merger::~Merger()
{

}

void Merger::startMerging()
{
    // A Merger instance is reused for the lifetime of the window. The old
    // implementation never cleared chosenMods, causing later merges to
    // silently include mods selected in previous runs.
    chosenMods.clear();
    uncookQueue.clear();
    processBuffer.clear();
    processErrored = false;
    outputStarted = false;
    nothingUncooked = true;
    shouldPause = settings->showPauseMessage;

    for (auto mod : modList) {
        if (mod->checked) {
            chosenMods.append(mod);
            if (mod->hasCache) {
                uncookQueue.enqueue(parseCmdArgs(settings->cmdUncook, mod->folderPathNative));
            }
        }
    }

    if (chosenMods.isEmpty()) {
        emit toLog(tr("No mods chosen!", "Log warning message."));
        emit mergingFailed(tr("No mods chosen."));
        return;
    }

    // Stale files in these folders contaminate the next pack. Refuse to run
    // instead of silently mixing data from a previous failed merge.
    if (directoryHasFiles(settings->pathUncooked) || directoryHasFiles(settings->pathCooked)) {
        emit toLog(tr("Working folders are not empty. Clean Uncooked and Cooked before merging."));
        emit mergingFailed(tr("Working folders are not empty."));
        return;
    }

    QString outputPath = settings->pathPacked + Constants::SLASH + settings->mergedModName;
    if (QDir(outputPath).exists()) {
        emit toLog(tr("Output folder already exists: %1").arg(outputPath));
        emit mergingFailed(tr("Merged output folder already exists."));
        return;
    }

    isRunning = true;
    emit mergingStarted();
}

void Merger::prepare()
{
    emit toLog(tr("Merging process started!", "Log message."));

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
    connect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &Merger::uncookFinished);

    uncookNext();
}

void Merger::uncookNext()
{
    emit toStatusbar(tr("Uncooking...", "Statusbar text (you can keep it untranslated if you want)."));

    if (!uncookQueue.isEmpty()) {
        QStringList args = uncookQueue.dequeue();
        QString name = args.value(1);
        name.remove(0, 7);
        emit toLog(tr("Uncooking: %1", "Log message (you can keep it untranslated if you want).").arg(name));
        startWcc(args);
    }
    else {
        disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
        emit startImagesDeleting();
    }
}

void Merger::uncookFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    processOutput();

    if (!processSucceeded(exitCode, exitStatus)) {
        if (harmlessUncookFailure()) {
            emit toLog(tr("No unbundled files were found; continuing with bundle extraction."));
        }
        else {
            abortMerge(tr("Uncooking failed. Source mods were not changed."));
            return;
        }
    }

    uncookNext();
}

void Merger::deleteImages()
{
    QString format = settings->cmdUncook.mid(settings->cmdUncook.indexOf("-imgfmt=") + QString("-imgfmt=").size(), 3);
    QDirIterator iter(settings->pathUncooked, QDir::Files, QDirIterator::Subdirectories);
    QString current;

    while (iter.hasNext()) {
        current = iter.next();

        if (current.endsWith(format, Qt::CaseInsensitive)) {
            QFile file(current);
            if (file.remove()) {
                emit toLog(tr("   %1 removed.", "File removal message.").arg(current));
            }
        }
    }

    pause(tr("Merging has been paused! Please delete unnecessary files and press \"OK\" to continue.", "Pause message (check Merger page for more info)."),
          "Uncooked",
          settings->pathUncooked,
          true);

    // Determine this from what actually exists, not merely from whether an
    // uncook command was queued. Some texture-cache mods contain no files WCC
    // can uncook, which is a valid bundle-only case.
    nothingUncooked = !directoryHasFiles(settings->pathUncooked);

    if (nothingUncooked) {
        emit skipCooking();
    }
    else {
        emit startCooking();
    }
}

void Merger::cookAll()
{
    emit toLog(tr("\nCooking started...", "Log message (you can keep it untranslated if you want)."));
    emit toStatusbar(tr("Cooking...", "Statusbar text (you can keep it untranslated if you want)."));

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
    connect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &Merger::cookFinished);

    startWcc(parseCmdArgs(settings->cmdCook));
}

void Merger::cookFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    processOutput();
    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);

    if (!processSucceeded(exitCode, exitStatus)) {
        abortMerge(tr("Cooking failed. Source mods were not changed."));
        return;
    }

    unpackAll();
}

void Merger::unpackAll()
{
    emit toLog(tr("\nUnpacking bundles...", "Log message (you can keep it untranslated if you want)."));
    emit toStatusbar(tr("Unpacking...", "Statusbar text."));

    Unpacker* unpacker = new Unpacker(chosenMods, settings);

    if (nothingUncooked) {
        connect(unpacker, &Unpacker::finished, this, &Merger::packAll);
    }
    else {
        connect(unpacker, &Unpacker::finished, this, &Merger::cacheBuild);
    }

    connect(unpacker, &Unpacker::toLog, this, &Merger::toLog);
    connect(unpacker, &Unpacker::failed, this, &Merger::abortMerge);
    connect(unpacker, &Unpacker::finished, unpacker, &Unpacker::deleteLater);
    connect(unpacker, &Unpacker::failed, unpacker, &Unpacker::deleteLater);

    unpacker->run();
}

void Merger::cacheBuild()
{
    pause(tr("Merging has been paused again! Please delete unnecessary files and press \"OK\" to continue.", "Pause message (check Merger page for more info)."),
          "Cooked",
          settings->pathCooked,
          false);

    emit toLog(tr("\nCache building started...", "Log message."));
    emit toStatusbar(tr("Cache building...", "Statusbar text."));

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
    connect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &Merger::cacheFinished);

    outputStarted = true;
    startWcc(parseCmdArgs(settings->cmdCache));
}

void Merger::cacheFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    processOutput();
    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);

    if (!processSucceeded(exitCode, exitStatus)) {
        abortMerge(tr("Texture cache build failed. Partial output was removed."));
        return;
    }

    packAll();
}

void Merger::packAll()
{
    pause(tr("Merging has been paused again! Please delete unnecessary files and press \"OK\" to continue.", "Pause message (check Merger page for more info)."),
          "Cooked",
          settings->pathCooked,
          false);

    emit toLog(tr("\nPacking process started...", "Log message (you can keep it untranslated if you want)."));
    emit toStatusbar(tr("Packing...", "Statusbar text (you can keep it untranslated if you want)."));

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
    connect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &Merger::packFinished);

    outputStarted = true;
    startWcc(parseCmdArgs(settings->cmdPack));
}

void Merger::packFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    processOutput();
    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);

    if (!processSucceeded(exitCode, exitStatus)) {
        abortMerge(tr("Packing failed. Partial output was removed."));
        return;
    }

    generateMetadata();
}

void Merger::generateMetadata()
{
    emit toLog(tr("\nMetadata creation started...", "Log message (you can keep it untranslated if you want)."));
    emit toStatusbar(tr("Generating metadata...", "Statusbar text (you can keep it untranslated if you want)."));

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);
    connect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished),
            this, &Merger::metadataFinished);

    startWcc(parseCmdArgs(settings->cmdMetadata));
}

void Merger::metadataFinished(int exitCode, QProcess::ExitStatus exitStatus)
{
    processOutput();
    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);

    if (!processSucceeded(exitCode, exitStatus)) {
        abortMerge(tr("Metadata creation failed. Partial output was removed."));
        return;
    }

    QString reason;
    if (!validateOutput(&reason)) {
        abortMerge(reason);
        return;
    }

    if (!commitSourceMods(&reason)) {
        abortMerge(reason);
        return;
    }

    finish();
}

void Merger::finish()
{
    isRunning = false;
    outputStarted = false;
    uncookQueue.clear();
    chosenMods.clear();
    emit mergingFinished();
}

/** UTILS **/

QStringList Merger::parseCmdArgs(QString cmd)
{
    QStringList templateArgs;
    templateArgs << cmd.split(" ");

    QStringList parsedArgs;

    for (QString arg : templateArgs) {
        arg.replace("%UNCOOK%", settings->pathUncooked);
        arg.replace("%COOK%", settings->pathCooked);
        arg.replace("%PACK%", settings->pathPacked);
        arg.replace("%NAME%", settings->mergedModName);
        parsedArgs << arg;
    }

    return parsedArgs;
}

QStringList Merger::parseCmdArgs(QString cmd, QString path)
{
    QStringList templateArgs;
    templateArgs << cmd.split(" ");

    QStringList parsedArgs;

    for (QString arg : templateArgs) {
        arg.replace("%MOD%", path);
        arg.replace("%UNCOOK%", settings->pathUncooked);
        parsedArgs << arg;
    }

    return parsedArgs;
}

void Merger::startWcc(const QStringList& args)
{
    processBuffer.clear();
    processErrored = false;
    wcc->start(settings->pathWcc, args);
}

void Merger::processOutput()
{
    QString standardOutput = QString::fromLocal8Bit(wcc->readAllStandardOutput());
    QString standardError = QString::fromLocal8Bit(wcc->readAllStandardError());

    if (!standardOutput.isEmpty()) {
        processBuffer.append(standardOutput);
        emit toLog(standardOutput);
    }

    if (!standardError.isEmpty()) {
        processBuffer.append(standardError);
        emit toLog(standardError);
    }
}

bool Merger::processSucceeded(int exitCode, QProcess::ExitStatus exitStatus) const
{
    if (processErrored || exitStatus != QProcess::NormalExit || exitCode != 0) {
        return false;
    }

    const QString lower = processBuffer.toLower();
    if (lower.contains("wcc operation failed") ||
        lower.contains("file corruption") ||
        lower.contains("access violation")) {
        return false;
    }

    return true;
}

bool Merger::harmlessUncookFailure() const
{
    const QString lower = processBuffer.toLower();
    return lower.contains("no unbundled files found") ||
           lower.contains("no unbundled files were found");
}

bool Merger::directoryHasFiles(const QString& path) const
{
    QDir directory(path);
    if (!directory.exists()) {
        return false;
    }

    QDirIterator iter(path, QDir::Files | QDir::NoSymLinks, QDirIterator::Subdirectories);
    return iter.hasNext();
}

bool Merger::validateOutput(QString* reason) const
{
    QString contentPath = settings->pathPacked + Constants::SLASH +
                          settings->mergedModName + Constants::CONTENT_PFIX;
    QString metadataPath = contentPath + Constants::METADATA_PFIX;

    QFileInfo metadataInfo(metadataPath);
    if (!metadataInfo.exists() || !metadataInfo.isFile() || metadataInfo.size() == 0) {
        *reason = tr("Merged output validation failed: metadata.store is missing or empty.");
        return false;
    }

    MetadataStore metadata;
    metadata.setFile(metadataPath);
    metadata.parse();

    if (!metadata.isValid() || metadata.bundlesList.isEmpty()) {
        *reason = tr("Merged output validation failed: metadata.store contains no valid bundles.");
        return false;
    }

    for (const QString& bundleName : metadata.bundlesList) {
        QFileInfo bundle(contentPath + Constants::SLASH + bundleName);
        if (!bundle.exists() || !bundle.isFile() || bundle.size() == 0) {
            *reason = tr("Merged output validation failed: bundle is missing or empty: %1").arg(bundleName);
            return false;
        }
    }

    return true;
}

bool Merger::commitSourceMods(QString* reason)
{
    QList<Mod*> committed;

    for (Mod* mod : chosenMods) {
        if (!mod->renameMerge()) {
            bool rollbackOk = true;
            for (int i = committed.size() - 1; i >= 0; --i) {
                if (!committed.at(i)->renameUnmerge()) {
                    rollbackOk = false;
                }
            }

            if (rollbackOk) {
                *reason = tr("Could not disable source mod %1. All source changes were rolled back.").arg(mod->modName);
            }
            else {
                *reason = tr("Could not disable source mod %1 and rollback was incomplete. Restore source mods before continuing.").arg(mod->modName);
            }
            return false;
        }

        committed.append(mod);
    }

    emit toLog(tr("Merged output validated. Source mods committed successfully."));
    return true;
}

void Merger::abortMerge(const QString& reason)
{
    if (!isRunning) {
        // Preflight failures happen before the running state is entered.
        emit mergingFailed(reason);
        return;
    }

    disconnect(wcc, static_cast<void(QProcess::*)(int, QProcess::ExitStatus)>(&QProcess::finished), 0, 0);

    if (wcc->state() != QProcess::NotRunning) {
        wcc->kill();
        wcc->waitForFinished(1000);
        processOutput();
    }

    if (outputStarted) {
        QString outputPath = settings->pathPacked + Constants::SLASH + settings->mergedModName;
        QDir(outputPath).removeRecursively();
    }

    isRunning = false;
    outputStarted = false;
    uncookQueue.clear();
    chosenMods.clear();

    emit toLog(tr("\nMERGE ABORTED: %1").arg(reason));
    emit toStatusbar(tr("Merge failed."));
    emit mergingFailed(reason);
}

void Merger::pause(QString message, QString folder, QString path, bool showNextTime)
{
    if (shouldPause) {
        shouldPause = showNextTime;
        PauseMessagebox msg(message, folder, path);

        emit toStatusbar(tr("Paused...", "Statusbar text.") );
        emit toLog(tr("\nMerging is paused so you can delete unnecessary files from the %1 folder.", "Pause log message.").arg(folder));

        if (folder == "Uncooked") {
            emit toLog(tr("Please remember which files were deleted because you have to delete the same files from the Cooked folder during the next pause.", "Pause log message."));
        }

        msg.exec();
    }
}
