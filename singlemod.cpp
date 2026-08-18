#include "singlemod.h"

#include <QDataStream>
#include <QPair>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

Mod::Mod(QString path, QObject* parent) : QObject(parent),  folderPath(path)
{
    using namespace Constants;

    folderPathNative = QDir::toNativeSeparators(folderPath);
    contentPath = folderPath + CONTENT_PFIX;
    scriptsPath = contentPath + SCRIPTS_PFIX;
    cachePath = contentPath + CACHE_PFIX;
    modName = folderPath.mid( folderPath.lastIndexOf(SLASH) + 1 );

    // NG-created packs are self-describing. The manifest lives at the pack
    // root using a RED-ignored '~' filename so it travels with the ModPack
    // without becoming game content.
    QFile manifestFile(folderPath + NG_MANIFEST_PFIX);
    if (manifestFile.open(QIODevice::ReadOnly)) {
        QJsonParseError parseError;
        const QJsonDocument manifest = QJsonDocument::fromJson(manifestFile.readAll(), &parseError);

        if (parseError.error == QJsonParseError::NoError && manifest.isObject()) {
            const QJsonObject object = manifest.object();
            const QJsonArray sources = object.value("sources").toArray();

            if (object.value("tool").toString() == "W3ModMerger-NG" &&
                object.value("schema").toInt() >= 1 &&
                !sources.isEmpty()) {
                isNgPack = true;
                for (const QJsonValue& source : sources) {
                    const QString sourceName = source.toString().trimmed();
                    if (!sourceName.isEmpty()) {
                        ngSources.append(sourceName);
                    }
                }
            }
        }
    }

    metadata.setFile(contentPath + METADATA_PFIX);
    metadata.parse();

    checker = new QFileInfo;

    checker->setFile(scriptsPath);
    hasScripts = checker->exists() && checker->isDir();

    checker->setFile(cachePath);
    hasCache = checker->exists() && checker->isFile() && checker->size() > 0;

    hasMetadata = metadata.isValid();

    if ( hasMetadata ) {
        for (QString bundleName : metadata.bundlesList) {
            bundles.append( new BundleFile( contentPath + SLASH + bundleName, this ) );
        }
    }

    hasBundles = bundles.size() > 0;

    QDir folder(contentPath);
    folder.setNameFilters(QStringList("*" + RENAME_PFIX));
    folder.setFilter(QDir::Files);
    mergedResources = folder.entryInfoList();

    modState = ( mergedResources.size() > 0 ) ? MERGED : NOT_MERGED;
    isMergeable = (hasMetadata && hasBundles) || modState == MERGED;

    folder.setNameFilters(QStringList({"*.bundle", "*.store", "*.cache"}));
    folder.setFilter(QDir::Files);
    QFileInfoList detectedResources = folder.entryInfoList();

    /* Compact, user-facing notes. Detailed reasons stay available through
       tooltips/conflict review instead of repeating paragraph warnings. */

    if (isNgPack) {
        modState = MERGED_PACK;
        checked = false;
        notes = tr("Verified NG Pack • %1 source mod(s)").arg(ngSources.size());
    }
    else if ( mergedResources.size() > 0 && detectedResources.size() > 0) {
        notes.append( tr("Mixed merged/unmerged resources — repair required.", "Incorrect mod structure warning.") );
        modState = CORRUPTED;
    }

    if (isMergeable && !isNgPack) {
        for (QString line : metadata.filesList) {
            if (line.indexOf(ICON_CHECK) != -1) {
                isMergeable = false;
                notes = tr("Inventory/icon resources — excluded from automatic merge suggestions.");
                break;
            }

            if (line.indexOf(XML_CHECK) != -1) {
                notes = tr("XML content — review before merging.", "Warns about detected xmls.");
                break;
            }

            if (line.indexOf(SWF_CHECK) != -1) {
                notes = tr("SWF content — review before merging.", "Warns about detected swfs.");
                break;
            }
        }
    }
}

bool Mod::renameMerge()
{
    using namespace Constants;

    QList<QPair<QString, QString>> pending;

    checker->setFile(cachePath);
    if ( checker->exists() ) {
        pending.append(qMakePair(cachePath, cachePath + RENAME_PFIX));
    }

    if (hasMetadata) {
        pending.append(qMakePair(contentPath + METADATA_PFIX,
                                 contentPath + METADATA_PFIX + RENAME_PFIX));
    }

    if (hasBundles) {
        for (auto file : bundles) {
            pending.append(qMakePair(file->fullPath, file->fullPath + RENAME_PFIX));
        }
    }

    // Preflight the complete transaction before touching the source mod.
    for (const auto& item : pending) {
        if (!QFileInfo::exists(item.first) || QFileInfo::exists(item.second)) {
            return false;
        }
    }

    QList<QPair<QString, QString>> completed;
    for (const auto& item : pending) {
        if (!QFile::rename(item.first, item.second)) {
            for (auto iter = completed.crbegin(); iter != completed.crend(); ++iter) {
                QFile::rename(iter->second, iter->first);
            }
            return false;
        }
        completed.append(item);
    }

    mergedResources.clear();
    for (const auto& item : completed) {
        mergedResources.append(QFileInfo(item.second));
    }
    modState = MERGED;
    return true;
}

bool Mod::renameUnmerge()
{
    QList<QPair<QString, QString>> pending;

    for (const QFileInfo& mr : mergedResources) {
        QString oldName = mr.absoluteFilePath();
        QString newName = oldName;
        newName.chop(Constants::RENAME_PFIX.size());
        pending.append(qMakePair(oldName, newName));
    }

    for (const auto& item : pending) {
        if (!QFileInfo::exists(item.first) || QFileInfo::exists(item.second)) {
            return false;
        }
    }

    QList<QPair<QString, QString>> completed;
    for (const auto& item : pending) {
        if (!QFile::rename(item.first, item.second)) {
            for (auto iter = completed.crbegin(); iter != completed.crend(); ++iter) {
                QFile::rename(iter->second, iter->first);
            }
            return false;
        }
        completed.append(item);
    }

    mergedResources.clear();
    modState = NOT_MERGED;
    return true;
}
