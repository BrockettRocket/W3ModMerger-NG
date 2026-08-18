#ifndef MERGER_H
#define MERGER_H

#include <QObject>
#include <QQueue>
#include <QProcess>

class Mod;
class Settings;

class Merger : public QObject
{
    Q_OBJECT

public:
    Merger(const QList<Mod*>& mods, const Settings* s, QObject* parent = 0);
    ~Merger();

    bool isRunning = false;
    bool nothingUncooked = true;
    bool shouldPause = false;

    void startMerging();
    void prepare();
    void uncookNext();
    void deleteImages();
    void cookAll();
    void unpackAll();
    void cacheBuild();
    void packAll();
    void generateMetadata();
    void finish();

signals:
    // Full process
    void mergingStarted();
    void startImagesDeleting();
    void startCooking();
    void mergingFinished();
    void mergingFailed(QString reason);
    //No cooking & cache
    void skipCooking();

    //Messaging
    void toLog(QString message);
    void toStatusbar(QString message);

private:
    const QList<Mod*>& modList;
    QList<Mod*> chosenMods;
    QQueue<QStringList> uncookQueue;

    const Settings* settings;
    QProcess* wcc;

    QString processBuffer;
    bool processErrored = false;
    bool outputStarted = false;

    QStringList parseCmdArgs(QString cmd);
    QStringList parseCmdArgs(QString cmd, QString path);
    void processOutput();
    void pause(QString message, QString folder, QString path, bool showNextTime);

    void startWcc(const QStringList& args);
    void uncookFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void cookFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void cacheFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void packFinished(int exitCode, QProcess::ExitStatus exitStatus);
    void metadataFinished(int exitCode, QProcess::ExitStatus exitStatus);

    bool processSucceeded(int exitCode, QProcess::ExitStatus exitStatus) const;
    bool harmlessUncookFailure() const;
    bool directoryHasFiles(const QString& path) const;
    bool validateOutput(QString* reason) const;
    bool commitSourceMods(QString* reason);
    void abortMerge(const QString& reason);
};

#endif // MERGER_H
