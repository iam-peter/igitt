#include "AskPass.h"

#include "GitRunner.h"

#include <QCoreApplication>
#include <QJSEngine>
#include <QLocalServer>
#include <QLocalSocket>
#include <QProcessEnvironment>
#include <QRandomGenerator>
#include <QRegularExpression>

#include <cstdio>

namespace {

// The socket of the igitt that asks, in the helper's environment
constexpr char ServerVariable[] = "IGITT_ASKPASS";

// Each message ends with a zero byte. The helper sends the prompt; igitt
// answers "1" and the text, or "0" for no answer.
constexpr char End = '\0';

} // namespace

AskPass::AskPass(QObject *parent)
    : QObject(parent)
{
}

AskPass *AskPass::instance()
{
    static AskPass *askPass = new AskPass(QCoreApplication::instance());
    return askPass;
}

AskPass *AskPass::create(QQmlEngine *, QJSEngine *)
{
    AskPass *askPass = instance();
    QJSEngine::setObjectOwnership(askPass, QJSEngine::CppOwnership);
    return askPass;
}

bool AskPass::running() const
{
    return m_server && m_server->isListening();
}

bool AskPass::isSecret(const QString &prompt)
{
    static const QRegularExpression secret(QStringLiteral("password|passphrase|\\bpin\\b|token|otp"),
                                           QRegularExpression::CaseInsensitiveOption);
    return secret.match(prompt).hasMatch();
}

bool AskPass::start(const QString &helper)
{
    if (running())
        return true;
    m_server = new QLocalServer(this);
    // Only this user's processes may ask, or read the answers
    m_server->setSocketOptions(QLocalServer::UserAccessOption);
    const QString name = QStringLiteral("igitt-askpass-%1-%2")
                             .arg(QCoreApplication::applicationPid())
                             .arg(QRandomGenerator::global()->generate(), 8, 16, QLatin1Char('0'));
    if (!m_server->listen(name)) {
        delete m_server;
        m_server = nullptr;
        return false;
    }
    connect(m_server, &QLocalServer::newConnection, this, [this] {
        while (QLocalSocket *socket = m_server->nextPendingConnection()) {
            const int id = m_nextId++;
            connect(socket, &QLocalSocket::readyRead, this, [this, socket, id] {
                if (m_waiting.contains(id))
                    return; // one prompt per helper
                QByteArray prompt = socket->property("buffer").toByteArray() + socket->readAll();
                const qsizetype end = prompt.indexOf(End);
                if (end < 0) {
                    socket->setProperty("buffer", prompt);
                    return;
                }
                prompt.truncate(end);
                m_waiting.insert(id, socket);
                const QString text = QString::fromUtf8(prompt);
                emit promptRequested(id, text, isSecret(text));
            });
            connect(socket, &QLocalSocket::disconnected, this, [this, socket, id] {
                if (m_waiting.remove(id))
                    emit promptCanceled(id);
                socket->deleteLater();
            });
        }
    });

    QProcessEnvironment env;
    env.insert(QString::fromLatin1(ServerVariable), m_server->fullServerName());
    env.insert(QStringLiteral("GIT_ASKPASS"), helper);
    env.insert(QStringLiteral("SSH_ASKPASS"), helper);
    // Even when igitt was started from a terminal, which ssh would ask on
    // unseen otherwise
    env.insert(QStringLiteral("SSH_ASKPASS_REQUIRE"), QStringLiteral("force"));
    GitRunner::setSharedEnvironment(env);
    return true;
}

void AskPass::reply(int id, const QByteArray &message)
{
    const QPointer<QLocalSocket> socket = m_waiting.take(id);
    if (!socket)
        return;
    socket->write(message + End);
    socket->flush();
    socket->disconnectFromServer();
}

void AskPass::answer(int id, const QString &text)
{
    reply(id, '1' + text.toUtf8());
}

void AskPass::cancel(int id)
{
    reply(id, "0");
}

bool AskPass::isHelperCall(int argc)
{
    return argc == 2 && qEnvironmentVariableIsSet(ServerVariable);
}

int AskPass::runHelper(const QString &prompt)
{
    QLocalSocket socket;
    socket.connectToServer(qEnvironmentVariable(ServerVariable));
    if (!socket.waitForConnected(5000))
        return 1;
    socket.write(prompt.toUtf8() + End);
    if (!socket.waitForBytesWritten(5000))
        return 1;
    // As long as the user takes to answer
    QByteArray reply;
    while (!reply.contains(End)) {
        if (!socket.waitForReadyRead(-1)) {
            reply += socket.readAll();
            break;
        }
        reply += socket.readAll();
    }
    reply.truncate(reply.indexOf(End) < 0 ? reply.size() : reply.indexOf(End));
    if (!reply.startsWith('1'))
        return 1;
    const QByteArray text = reply.mid(1) + '\n';
    std::fwrite(text.constData(), 1, size_t(text.size()), stdout);
    std::fflush(stdout);
    return 0;
}
