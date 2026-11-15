#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtQml/qqmlregistration.h>

class QJSEngine;
class QLocalServer;
class QLocalSocket;
class QQmlEngine;

// Asks the user what git or ssh wants to know, a user name, a password or
// a key's passphrase, instead of the terminal igitt has none of.
//
// git and ssh run GIT_ASKPASS and SSH_ASKPASS with the prompt as the only
// argument and read the answer from its output. Both name igitt itself,
// which, started that way (see isHelperCall()), passes the prompt on to the
// igitt that runs git through a local socket and prints what comes back.
class AskPass : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

public:
    static AskPass *instance();
    static AskPass *create(QQmlEngine *, QJSEngine *);

    // Serves the prompts of every git command run from now on, `helper`
    // being the program to run for them: igitt's own executable.
    bool start(const QString &helper);
    bool running() const;

    // Whether this process was started by git or ssh to ask something
    static bool isHelperCall(int argc);
    // Asks the igitt that ran git, and prints the answer: the exit code
    static int runHelper(const QString &prompt);

    // A password, passphrase or PIN, not to be shown
    static bool isSecret(const QString &prompt);

    Q_INVOKABLE void answer(int id, const QString &text);
    Q_INVOKABLE void cancel(int id);

signals:
    // `secret`: the answer is to be hidden while typed
    void promptRequested(int id, const QString &prompt, bool secret);
    void promptCanceled(int id); // the helper went away unanswered

private:
    explicit AskPass(QObject *parent = nullptr);
    void reply(int id, const QByteArray &message);

    QLocalServer *m_server = nullptr;
    QHash<int, QPointer<QLocalSocket>> m_waiting;
    int m_nextId = 1;
};
