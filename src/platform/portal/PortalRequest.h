#pragma once

#include <QDBusMessage>
#include <QObject>
#include <QVariantMap>

namespace surr {

/**
 * Uma chamada ao portal que devolve um objeto Request.
 *
 * A ordem aqui é a razão de a classe existir: o construtor gera o
 * `handle_token`, prevê o caminho do Request, **conecta o sinal `Response`**
 * e só então dispara a chamada assíncrona. Conectar depois perde o sinal
 * quando o portal responde rápido — falha que se manifesta como travamento,
 * não como erro.
 */
class PortalRequest : public QObject
{
    Q_OBJECT
public:
    /**
     * @param interface  ex.: "org.freedesktop.portal.GlobalShortcuts"
     * @param method     ex.: "CreateSession"
     * @param args       argumentos anteriores ao dicionário de opções
     * @param options    opções; `handle_token` é acrescentado aqui
     */
    static PortalRequest *call(const QString &interface,
                               const QString &method,
                               const QVariantList &args,
                               QVariantMap options,
                               QObject *parent = nullptr);

    /// Timeout de segurança; padrão 120 s (diálogos esperam o usuário).
    void setTimeout(int ms);

Q_SIGNALS:
    /// @p response: 0 = sucesso, 1 = cancelado pelo usuário, 2 = outro erro.
    void finished(uint response, const QVariantMap &results);
    void failed(const QString &error);

private Q_SLOTS:
    // Conectado pela sintaxe antiga (SLOT macro) porque o sinal vem de um
    // caminho de objeto conhecido só em runtime.
    void onResponse(uint response, const QVariantMap &results);

private:
    explicit PortalRequest(QObject *parent);

    struct Private;
    Private *d;
};

} // namespace surr
