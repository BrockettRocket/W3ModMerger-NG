/**
 *  Compression types:
 *  0 - Not compressed
 *  1 - Zlib
 *  2 - Doboz ???
 *  3 - Doboz
 *  4 - Lz4 (not used)
 *  5 - Lz4
 *
 *  UPDATE: looks like mods packed with lz4 only
 */

#include "unpacker.h"
#include "libs/lz4.h"

#include <QThread>
#include <cstring>

Unpacker::Unpacker(QList<Mod*>& list, const Settings* s) : modsList(list), settings(s)
{

}

void Unpacker::run()
{
    QThread* thread = new QThread;

    moveToThread(thread);

    connect(thread, &QThread::started, this, &Unpacker::startUnpacking);
    connect(this, &Unpacker::finished, thread, &QThread::quit);
    connect(this, &Unpacker::failed, thread, &QThread::quit);
    connect(thread, &QThread::finished, thread, &QThread::deleteLater);

    thread->start();
}

void Unpacker::startUnpacking()
{
    for (auto mod : modsList) {

        for (BundleFile* bundle : mod->bundles) {

            QFile source(bundle->fullPath);

            if (!source.open(QIODevice::ReadOnly)) {
                QString reason = tr("Failed to open source bundle: %1").arg(bundle->fullPath);
                emit toLog(reason);
                emit failed(reason);
                return;
            }

            for (FileRecord record : bundle->fileList) {

                emit toLog( tr("   Extracting: %1", "File extraction message.").arg(record.filename) );

                // Bundle paths use backslashes, but some valid resources live
                // directly at the bundle root (for example strings.list).
                // The old mid(0, -1) behaviour turned a root-level filename
                // into a directory named after the file, making QFile::open()
                // fail immediately. Normalize separators and only create a
                // parent directory when one actually exists.
                QString relativePath = record.filename;
                relativePath.replace('\\', '/');

                const int separator = relativePath.lastIndexOf('/');
                if (separator >= 0) {
                    const QString dirPath = relativePath.left(separator);
                    QDir d;
                    if (!d.mkpath(settings->pathCooked + Constants::SLASH + dirPath)) {
                        QString reason = tr("Failed to create output directory for: %1").arg(record.filename);
                        emit toLog(reason);
                        emit failed(reason);
                        return;
                    }
                }

                QFile result(settings->pathCooked + Constants::SLASH + relativePath);

                if (!result.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                    QString reason = tr("Failed to save extracted file: %1").arg(result.fileName());
                    emit toLog(reason);
                    emit failed(reason);
                    return;
                }

                if (!source.seek(record.globalOffset)) {
                    QString reason = tr("Failed to seek source bundle while extracting: %1").arg(record.filename);
                    emit toLog(reason);
                    emit failed(reason);
                    return;
                }

                QByteArray encoded = source.read(record.sizeCompressed);
                if (encoded.size() != record.sizeCompressed) {
                    QString reason = tr("Unexpected end of bundle while extracting: %1").arg(record.filename);
                    emit toLog(reason);
                    emit failed(reason);
                    return;
                }

                QByteArray decoded;
                decoded.resize(record.sizeUncompressed);

                if (!unpack(record.algorithm,
                            record.sizeCompressed,
                            record.sizeUncompressed,
                            encoded.data(), decoded.data())) {
                    QString reason = tr("Failed to decompress: %1 (algorithm %2)")
                                         .arg(record.filename)
                                         .arg(record.algorithm);
                    emit toLog(reason);
                    emit failed(reason);
                    return;
                }

                if (result.write(decoded.constData(), decoded.size()) != decoded.size()) {
                    QString reason = tr("Failed to write complete extracted file: %1").arg(result.fileName());
                    emit toLog(reason);
                    emit failed(reason);
                    return;
                }

                result.close();
            }
            source.close();
        }
    }

    // Source mods are intentionally left untouched here. They are only
    // disabled after the final merged output has been validated.
    emit finished();
}

bool Unpacker::unpack(int algo, int sizec, int sizeu, char* buf_compressed, char* buf_uncompressed)
{
    if (algo == 0) {
        if (sizec != sizeu) {
            return false;
        }
        std::memcpy(buf_uncompressed, buf_compressed, sizeu);
        return true;
    }

    if (algo == 5) {
        int result = LZ4_decompress_safe(buf_compressed, buf_uncompressed, sizec, sizeu);
        return result == sizeu;
    }

    emit toLog("Unsupported compression algorithm: " + QString::number(algo));
    return false;
}
