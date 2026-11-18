#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QThread>
#include <iostream>

// Test executable only. Installed under a separate fixture deployment; never
// copied over the user's solver. Its output must not control the frontend.
int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    QFile pointer(QStringLiteral("D:/IIIDIR.DAT"));
    if (!pointer.open(QIODevice::ReadOnly)) return 90;
    const QString line = QString::fromLocal8Bit(pointer.readLine());
    const int first = line.indexOf('\'');
    const int last = line.indexOf('\'', first + 1);
    if (first < 0 || last <= first) return 91;
    QDir root(QDir::fromNativeSeparators(line.mid(first + 1, last - first - 1)));
    const auto write = [&root](const QString& path, const QByteArray& content) {
        QFile f(root.filePath(path));
        return f.open(QIODevice::WriteOnly) && f.write(content) == content.size();
    };
    QElapsedTimer elapsed;
    elapsed.start();
    root.mkpath(QStringLiteral("CASE01MODE"));
    root.mkpath(QStringLiteral("CASE01WAVE"));
    QStringList key;
    for (int i = 0; i < 24; ++i) key.append(QStringLiteral("fixture header"));
    key[14] = QStringLiteral("1 .5 .25 0 100 .0002");
    key[20] = QStringLiteral("3 7 0 0");
    if (!write(QStringLiteral("CASE01MODE/IKEY.DAT"), key.join('\n').toLatin1())
        || !write(QStringLiteral("CASE01WAVE/IKEY.DAT"), key.join('\n').toLatin1())
        || !write(QStringLiteral("III110.DAT"), "ERROR fixture diagnostic, not a real solver error\n")) return 92;

    std::cout << "FIXTURE_ONLY: mismatched controls and invalid residuals follow" << std::endl;
    std::cout << "ERROR fixture stdout; exit status remains authoritative" << std::endl;
    std::cerr << "FIXTURE_STDERR" << std::endl;
    std::cout << "IT=1 tol=0.001 ERR=NaN IFHG=1\nISTEP=1 ER=NaN HG=1" << std::endl;
    // A misleading completion prompt must not finish/kill the process early.
    std::cout << "PRESS ANY KEY: fixture continues for two seconds" << std::endl;
    QThread::msleep(2000);
    const int code = qEnvironmentVariableIntValue("CAE_FIXTURE_EXIT_CODE");
    const QJsonObject result{{"fixture_only", true}, {"elapsed_ms", double(elapsed.elapsed())},
                             {"exit_code", code}};
    if (!write(QStringLiteral("fixture_natural_exit.json"), QJsonDocument(result).toJson())) return 93;
    std::cout << "FIXTURE_NATURAL_EXIT=" << code << std::endl;
    return code;
}
