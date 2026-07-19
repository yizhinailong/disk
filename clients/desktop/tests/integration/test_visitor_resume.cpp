#include <QCryptographicHash>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QTemporaryDir>
#include <QTest>
#include <QTimer>
#include <QUrlQuery>
#include <QUuid>

#include "managers/TransferManager.hpp"
#include "network/NetworkClient.hpp"
#include "network/RequestFactory.hpp"

using namespace disk::desktop;
using namespace disk::desktop::managers;

namespace {

    constexpr auto REQUEST_TIMEOUT_MS = 15000;
    constexpr auto DOWNLOAD_TIMEOUT_MS = 30000;
    constexpr qsizetype PAYLOAD_SIZE = 256 * 1024 + 37;
    constexpr qsizetype PARTIAL_SIZE = 64 * 1024 + 13;

    struct ApiResponse {
        int status_code{ 0 };
        int api_code{ -1 };
        QNetworkReply::NetworkError network_error{ QNetworkReply::UnknownNetworkError };
        QJsonObject json;
        bool timed_out{ false };
        bool valid_json{ false };
    };

    auto make_payload() -> QByteArray {
        QByteArray payload;
        payload.resize(PAYLOAD_SIZE);
        for (qsizetype index = 0; index < payload.size(); ++index) {
            payload[index] = static_cast<char>((index * 31 + 17) % 251);
        }
        return payload;
    }

    auto md5(const QByteArray& data) -> QString {
        return QString(QCryptographicHash::hash(data, QCryptographicHash::Md5).toHex());
    }

    auto json_id(const QJsonValue& value) -> QString {
        if (value.isString()) {
            return value.toString();
        }
        if (value.isDouble()) {
            return QString::number(static_cast<qulonglong>(value.toDouble()));
        }
        return {};
    }

    auto api_code(const QJsonValue& value) -> int {
        if (value.isDouble()) {
            return value.toInt(-1);
        }
        if (value.isString()) {
            bool ok = false;
            const int code = value.toString().toInt(&ok);
            return ok ? code : -1;
        }
        return -1;
    }

    class BackendFixture {
    public:
        explicit BackendFixture(NetworkClient* network_client)
            : m_network_client(network_client) {}

        ~BackendFixture() {
            Cleanup();
        }

        BackendFixture(const BackendFixture&) = delete;
        auto operator=(const BackendFixture&) -> BackendFixture& = delete;

        auto Provision(const QByteArray& payload) -> bool {
            m_payload_hash = md5(payload);
            m_nonce = QUuid::createUuid().toString(QUuid::WithoutBraces);
            m_nonce.remove('-');

            if (!LoginAdmin() || !RegisterOwner() || !LoginOwner()) {
                return false;
            }
            if (!UploadPayload(payload) || !CreateShare() || !AccessShare()) {
                return false;
            }
            return true;
        }

        auto Cleanup() -> bool {
            if (m_cleanup_done) {
                return m_cleanup_succeeded;
            }
            m_cleanup_done = true;
            m_cleanup_succeeded = true;

            if (!m_upload_id.isEmpty() && !m_owner_token.isEmpty()) {
                const auto response = Send(m_network_client->Delete(
                    QUrl(QStringLiteral("/api/file/upload/%1").arg(m_upload_id)),
                    OwnerHeaders()
                ));
                RecordCleanupResult(QStringLiteral("cancel upload"), response);
            }

            if (!m_share_id.isEmpty() && !m_owner_token.isEmpty()) {
                QJsonArray share_ids;
                share_ids.append(m_share_id);
                const auto response = PostJson(
                    QUrl(QStringLiteral("/api/share/cancel")),
                    QJsonObject{
                        { "share_ids", share_ids }
                },
                    OwnerHeaders()
                );
                const bool operation_succeeded = ShareCancelSucceeded(response);
                RecordCleanupOperationResult(
                    QStringLiteral("cancel share"),
                    response,
                    operation_succeeded
                );
            }

            if (m_file_id != 0 && !m_owner_token.isEmpty()) {
                QJsonArray file_ids;
                file_ids.append(static_cast<double>(m_file_id));
                const auto soft_delete = DeleteJson(
                    QUrl(QStringLiteral("/api/file")),
                    QJsonObject{
                        { "file_ids", file_ids }
                },
                    OwnerHeaders()
                );
                const bool soft_delete_succeeded = FileDeleteSucceeded(soft_delete);
                RecordCleanupOperationResult(
                    QStringLiteral("soft delete file"),
                    soft_delete,
                    soft_delete_succeeded
                );

                if (soft_delete_succeeded) {
                    QJsonArray trash_items;
                    if (ListTrashItems(
                            QStringLiteral("list trash after soft delete"),
                            trash_items
                        )) {
                        const quint64 trash_id = FindTrashId(trash_items, m_file_id);
                        if (trash_id == 0) {
                            RecordCleanupFailure(
                                QStringLiteral("soft-deleted file has no trash record")
                            );
                        } else {
                            QJsonArray trash_ids;
                            trash_ids.append(static_cast<double>(trash_id));
                            const auto permanent_delete = PostJson(
                                QUrl(QStringLiteral("/api/trash/delete")),
                                QJsonObject{
                                    { "trash_ids", trash_ids }
                            },
                                OwnerHeaders()
                            );
                            const bool permanent_delete_succeeded =
                                TrashDeleteSucceeded(permanent_delete);
                            RecordCleanupOperationResult(
                                QStringLiteral("permanently delete file"),
                                permanent_delete,
                                permanent_delete_succeeded
                            );

                            if (permanent_delete_succeeded) {
                                QJsonArray remaining_items;
                                if (ListTrashItems(
                                        QStringLiteral("verify trash cleanup"),
                                        remaining_items
                                    ) &&
                                    FindTrashId(remaining_items, m_file_id) != 0) {
                                    RecordCleanupFailure(
                                        QStringLiteral("permanently deleted file remains in trash")
                                    );
                                }
                            }
                        }
                    }
                }
            }

            if (m_owner_user_id != 0 && !m_admin_token.isEmpty()) {
                const auto response = Send(m_network_client->Delete(
                    QUrl(QStringLiteral("/api/admin/users/%1").arg(m_owner_user_id)),
                    AuthorizationHeaders(m_admin_token)
                ));
                RecordCleanupResult(QStringLiteral("disable owner"), response);
            }

            m_admin_token.clear();
            m_owner_token.clear();
            m_share_token.clear();
            return m_cleanup_succeeded;
        }

        [[nodiscard]] auto Failure() const -> const QString& {
            return m_failure;
        }

        [[nodiscard]] auto CleanupFailure() const -> const QString& {
            return m_cleanup_failure;
        }

        [[nodiscard]] auto OwnerToken() const -> const QString& {
            return m_owner_token;
        }

        [[nodiscard]] auto ShareToken() const -> const QString& {
            return m_share_token;
        }

        [[nodiscard]] auto ShareId() const -> const QString& {
            return m_share_id;
        }

        [[nodiscard]] auto FileId() const -> quint64 {
            return m_file_id;
        }

        [[nodiscard]] auto PayloadHash() const -> const QString& {
            return m_payload_hash;
        }

    private:
        auto Send(QNetworkReply* reply) -> ApiResponse {
            ApiResponse response;
            if (!reply) {
                return response;
            }

            QEventLoop loop;
            QTimer timeout;
            timeout.setSingleShot(true);
            bool finished = false;

            QObject::connect(reply, &QNetworkReply::finished, &loop, [&]() {
                finished = true;
                loop.quit();
            });
            QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
            timeout.start(REQUEST_TIMEOUT_MS);
            loop.exec();

            if (!finished) {
                response.timed_out = true;
                reply->abort();
            } else {
                timeout.stop();
            }

            response.status_code =
                reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
            response.network_error = reply->error();

            QJsonParseError parse_error;
            const auto document = QJsonDocument::fromJson(reply->readAll(), &parse_error);
            response.valid_json =
                parse_error.error == QJsonParseError::NoError && document.isObject();
            if (response.valid_json) {
                response.json = document.object();
                response.api_code = api_code(response.json.value(QStringLiteral("code")));
            }

            reply->deleteLater();
            return response;
        }

        auto PostJson(
            const QUrl& url,
            const QJsonObject& body,
            QMap<QString, QString> headers
        ) -> ApiResponse {
            headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
            return Send(m_network_client->Post(
                url,
                QJsonDocument(body).toJson(QJsonDocument::Compact),
                headers
            ));
        }

        auto DeleteJson(
            const QUrl& url,
            const QJsonObject& body,
            QMap<QString, QString> headers
        ) -> ApiResponse {
            headers.insert(QStringLiteral("Content-Type"), QStringLiteral("application/json"));
            return Send(m_network_client->Delete(
                url,
                QJsonDocument(body).toJson(QJsonDocument::Compact),
                headers
            ));
        }

        static auto AuthorizationHeaders(const QString& token)
            -> QMap<QString, QString> {
            return {
                { QStringLiteral("Authorization"), QStringLiteral("Bearer %1").arg(token) },
            };
        }

        auto OwnerHeaders() const -> QMap<QString, QString> {
            return AuthorizationHeaders(m_owner_token);
        }

        static auto Succeeded(const ApiResponse& response) -> bool {
            return !response.timed_out &&
                   response.network_error == QNetworkReply::NoError &&
                   response.status_code >= 200 && response.status_code < 300 &&
                   response.valid_json && response.api_code == 0;
        }

        static auto ShareCancelSucceeded(const ApiResponse& response) -> bool {
            if (!Succeeded(response)) {
                return false;
            }
            const auto data =
                response.json.value(QStringLiteral("data")).toObject();
            const auto summary =
                data.value(QStringLiteral("summary")).toObject();
            const auto results =
                data.value(QStringLiteral("results")).toArray();
            return summary.value(QStringLiteral("total")).toInt(-1) == 1 &&
                   summary.value(QStringLiteral("succeeded")).toInt(-1) == 1 &&
                   summary.value(QStringLiteral("failed")).toInt(-1) == 0 &&
                   results.size() == 1 &&
                   results.first()
                           .toObject()
                           .value(QStringLiteral("status"))
                           .toString() == QStringLiteral("success");
        }

        static auto FileDeleteSucceeded(const ApiResponse& response) -> bool {
            if (!Succeeded(response)) {
                return false;
            }
            const auto data =
                response.json.value(QStringLiteral("data")).toObject();
            return data.value(QStringLiteral("deleted_count")).toInt(-1) == 1 &&
                   data.value(QStringLiteral("deleted_file_count")).toInt(-1) == 1;
        }

        static auto TrashDeleteSucceeded(const ApiResponse& response) -> bool {
            if (!Succeeded(response)) {
                return false;
            }
            const auto data =
                response.json.value(QStringLiteral("data")).toObject();
            const auto summary =
                data.value(QStringLiteral("summary")).toObject();
            const auto results =
                data.value(QStringLiteral("results")).toArray();
            return summary.value(QStringLiteral("total")).toInt(-1) == 1 &&
                   summary.value(QStringLiteral("success_count")).toInt(-1) == 1 &&
                   summary.value(QStringLiteral("failure_count")).toInt(-1) == 0 &&
                   results.size() == 1 &&
                   results.first()
                           .toObject()
                           .value(QStringLiteral("status"))
                           .toString() == QStringLiteral("success");
        }

        auto RequireSuccess(const QString& stage, const ApiResponse& response) -> bool {
            if (Succeeded(response)) {
                return true;
            }
            m_failure = QStringLiteral(
                            "%1 failed (HTTP %2, API %3, network %4, timeout %5)"
            )
                            .arg(stage)
                            .arg(response.status_code)
                            .arg(response.api_code)
                            .arg(static_cast<int>(response.network_error))
                            .arg(response.timed_out);
            return false;
        }

        void RecordCleanupResult(const QString& stage, const ApiResponse& response) {
            if (Succeeded(response)) {
                return;
            }
            RecordCleanupFailure(
                QStringLiteral("%1 (HTTP %2, API %3, network %4, timeout %5)")
                    .arg(stage)
                    .arg(response.status_code)
                    .arg(response.api_code)
                    .arg(static_cast<int>(response.network_error))
                    .arg(response.timed_out)
            );
        }

        void RecordCleanupOperationResult(
            const QString& stage,
            const ApiResponse& response,
            bool operation_succeeded
        ) {
            RecordCleanupResult(stage, response);
            if (Succeeded(response) && !operation_succeeded) {
                RecordCleanupFailure(
                    QStringLiteral("%1 returned an unsuccessful item result").arg(stage)
                );
            }
        }

        void RecordCleanupFailure(const QString& failure) {
            m_cleanup_succeeded = false;
            if (!m_cleanup_failure.isEmpty()) {
                m_cleanup_failure += QStringLiteral("; ");
            }
            m_cleanup_failure += failure;
        }

        auto ListTrashItems(const QString& stage, QJsonArray& items) -> bool {
            QUrl url(QStringLiteral("/api/trash"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("page"), QStringLiteral("1"));
            query.addQueryItem(QStringLiteral("page_size"), QStringLiteral("100"));
            url.setQuery(query);

            const auto response = Send(m_network_client->Get(url, OwnerHeaders()));
            RecordCleanupResult(stage, response);
            if (!Succeeded(response)) {
                return false;
            }

            items = response.json.value(QStringLiteral("data"))
                        .toObject()
                        .value(QStringLiteral("items"))
                        .toArray();
            return true;
        }

        static auto FindTrashId(const QJsonArray& items, quint64 file_id) -> quint64 {
            for (const auto& value : items) {
                const auto item = value.toObject();
                bool original_id_ok = false;
                const quint64 original_id =
                    json_id(item.value(QStringLiteral("original_id")))
                        .toULongLong(&original_id_ok);
                if (!original_id_ok || original_id != file_id ||
                    item.value(QStringLiteral("type")).toString() !=
                        QStringLiteral("file")) {
                    continue;
                }

                bool trash_id_ok = false;
                const quint64 trash_id =
                    json_id(item.value(QStringLiteral("id"))).toULongLong(&trash_id_ok);
                return trash_id_ok ? trash_id : 0;
            }
            return 0;
        }

        auto LoginAdmin() -> bool {
            QString admin_password =
                qEnvironmentVariable("DISK_DESKTOP_ADMIN_PASSWORD");
            if (admin_password.isEmpty()) {
                admin_password = qEnvironmentVariable("ADMIN_PASS");
            }
            if (admin_password.isEmpty()) {
                m_failure =
                    QStringLiteral("DISK_DESKTOP_ADMIN_PASSWORD or ADMIN_PASS is required");
                return false;
            }

            QString admin_account =
                qEnvironmentVariable("DISK_DESKTOP_ADMIN_ACCOUNT");
            if (admin_account.isEmpty()) {
                admin_account = qEnvironmentVariable("ADMIN_USER");
            }
            if (admin_account.isEmpty()) {
                admin_account = QStringLiteral("admin");
            }

            const auto response = PostJson(
                QUrl(QStringLiteral("/api/auth/login")),
                QJsonObject{
                    {  "account",  admin_account },
                    { "password", admin_password },
            },
                {}
            );
            if (!RequireSuccess(QStringLiteral("admin login"), response)) {
                return false;
            }

            m_admin_token = response.json.value(QStringLiteral("data"))
                                .toObject()
                                .value(QStringLiteral("access_token"))
                                .toString();
            if (m_admin_token.isEmpty()) {
                m_failure = QStringLiteral("admin login returned no access token");
                return false;
            }
            return true;
        }

        auto RegisterOwner() -> bool {
            m_owner_name = QStringLiteral("desktop_e2e_%1").arg(m_nonce.left(16));
            m_owner_password = QStringLiteral("DesktopA9%1").arg(m_nonce.left(20));
            const QString email =
                QStringLiteral("%1@example.com").arg(m_owner_name);

            const auto response = PostJson(
                QUrl(QStringLiteral("/api/auth/register")),
                QJsonObject{
                    { "username",     m_owner_name },
                    {    "email",            email },
                    { "password", m_owner_password },
            },
                {}
            );
            if (!RequireSuccess(QStringLiteral("owner registration"), response)) {
                return false;
            }

            const QString user_id =
                json_id(response.json.value(QStringLiteral("data"))
                            .toObject()
                            .value(QStringLiteral("user"))
                            .toObject()
                            .value(QStringLiteral("id")));
            bool ok = false;
            m_owner_user_id = user_id.toULongLong(&ok);
            if (!ok || m_owner_user_id == 0) {
                m_failure = QStringLiteral("owner registration returned no user id");
                return false;
            }
            return true;
        }

        auto LoginOwner() -> bool {
            const auto response = PostJson(
                QUrl(QStringLiteral("/api/auth/login")),
                QJsonObject{
                    {  "account",     m_owner_name },
                    { "password", m_owner_password },
            },
                {}
            );
            m_owner_password.clear();
            if (!RequireSuccess(QStringLiteral("owner login"), response)) {
                return false;
            }

            m_owner_token = response.json.value(QStringLiteral("data"))
                                .toObject()
                                .value(QStringLiteral("access_token"))
                                .toString();
            if (m_owner_token.isEmpty()) {
                m_failure = QStringLiteral("owner login returned no access token");
                return false;
            }
            return true;
        }

        auto UploadPayload(const QByteArray& payload) -> bool {
            const QString filename =
                QStringLiteral("desktop_resume_%1.bin").arg(m_nonce.left(16));
            const auto init_response = PostJson(
                QUrl(QStringLiteral("/api/file/upload/init")),
                QJsonObject{
                    {  "filename",                            filename },
                    { "file_size", static_cast<double>(payload.size()) },
                    { "file_hash",                      m_payload_hash },
                    { "parent_id",                                   0 },
            },
                OwnerHeaders()
            );
            if (!RequireSuccess(QStringLiteral("upload init"), init_response)) {
                return false;
            }

            const auto init_data =
                init_response.json.value(QStringLiteral("data")).toObject();
            if (init_data.value(QStringLiteral("instant_upload")).toBool(false)) {
                const QString file_id = json_id(init_data.value(QStringLiteral("file"))
                                                    .toObject()
                                                    .value(QStringLiteral("id")));
                bool ok = false;
                m_file_id = file_id.toULongLong(&ok);
                if (!ok || m_file_id == 0) {
                    m_failure = QStringLiteral("instant upload returned no file id");
                    return false;
                }
                return true;
            }

            m_upload_id =
                init_data.value(QStringLiteral("upload_id")).toString();
            if (m_upload_id.isEmpty()) {
                m_failure = QStringLiteral("upload init returned no upload id");
                return false;
            }

            QUrl chunk_url(QStringLiteral("/api/file/upload/chunk"));
            QUrlQuery query;
            query.addQueryItem(QStringLiteral("upload_id"), m_upload_id);
            query.addQueryItem(QStringLiteral("chunk_index"), QStringLiteral("0"));
            query.addQueryItem(QStringLiteral("chunk_hash"), m_payload_hash);
            chunk_url.setQuery(query);

            auto chunk_headers = OwnerHeaders();
            chunk_headers.insert(
                QStringLiteral("Content-Type"),
                QStringLiteral("application/octet-stream")
            );
            const auto chunk_response = Send(
                m_network_client->Post(chunk_url, payload, chunk_headers)
            );
            if (!RequireSuccess(QStringLiteral("upload chunk"), chunk_response)) {
                return false;
            }

            const auto complete_response = PostJson(
                QUrl(QStringLiteral("/api/file/upload/complete")),
                QJsonObject{
                    { "upload_id", m_upload_id }
            },
                OwnerHeaders()
            );
            if (!RequireSuccess(QStringLiteral("upload complete"), complete_response)) {
                return false;
            }

            const QString file_id =
                json_id(complete_response.json.value(QStringLiteral("data"))
                            .toObject()
                            .value(QStringLiteral("file"))
                            .toObject()
                            .value(QStringLiteral("id")));
            bool ok = false;
            m_file_id = file_id.toULongLong(&ok);
            if (!ok || m_file_id == 0) {
                m_failure = QStringLiteral("upload complete returned no file id");
                return false;
            }
            m_upload_id.clear();
            return true;
        }

        auto CreateShare() -> bool {
            QJsonArray file_ids;
            file_ids.append(static_cast<double>(m_file_id));
            const auto response = PostJson(
                QUrl(QStringLiteral("/api/share")),
                QJsonObject{
                    {    "file_ids",                   file_ids },
                    {  "permission", QStringLiteral("download") },
                    { "expire_days",                          1 },
            },
                OwnerHeaders()
            );
            if (!RequireSuccess(QStringLiteral("share creation"), response)) {
                return false;
            }

            m_share_id = response.json.value(QStringLiteral("data"))
                             .toObject()
                             .value(QStringLiteral("share_id"))
                             .toString();
            if (m_share_id.isEmpty()) {
                m_failure = QStringLiteral("share creation returned no share id");
                return false;
            }
            return true;
        }

        auto AccessShare() -> bool {
            const auto response = PostJson(
                QUrl(QStringLiteral("/api/share/access/%1").arg(m_share_id)),
                {},
                {}
            );
            if (!RequireSuccess(QStringLiteral("share access"), response)) {
                return false;
            }

            m_share_token = response.json.value(QStringLiteral("data"))
                                .toObject()
                                .value(QStringLiteral("share_token"))
                                .toString();
            if (m_share_token.isEmpty()) {
                m_failure = QStringLiteral("share access returned no share token");
                return false;
            }
            return true;
        }

        NetworkClient* m_network_client;
        QString m_failure;
        QString m_cleanup_failure;
        QString m_nonce;
        QString m_owner_name;
        QString m_owner_password;
        QString m_admin_token;
        QString m_owner_token;
        QString m_share_token;
        QString m_upload_id;
        QString m_share_id;
        QString m_payload_hash;
        quint64 m_owner_user_id{ 0 };
        quint64 m_file_id{ 0 };
        bool m_cleanup_done{ false };
        bool m_cleanup_succeeded{ false };
    };

    void write_resume_state(
        const QString& target_path,
        const QString& share_id,
        quint64 file_id,
        quint64 expected_size,
        quint64 partial_size,
        const QString& integrity_hash
    ) {
        QFile sidecar(target_path + QStringLiteral(".download.json"));
        if (!sidecar.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            return;
        }

        const QString remote_identity =
            QStringLiteral("visitor:%1:%2:%3:%4")
                .arg(
                    share_id,
                    QString::number(file_id),
                    QString::number(expected_size),
                    integrity_hash
                );
        const QJsonObject state{
            {    "remote_identity",                    remote_identity },
            {      "expected_size", static_cast<double>(expected_size) },
            { "local_partial_size",  static_cast<double>(partial_size) },
            {     "integrity_hash",                     integrity_hash },
        };
        sidecar.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
    }

    struct DownloadObservation {
        int request_count{ 0 };
        int status_code{ 0 };
        bool range_matches{ false };
        bool visitor_token_matches{ false };
        bool authorization_absent{ false };
    };

} // namespace

class TestVisitorResumeIntegration : public QObject {
    Q_OBJECT

private slots:

    void VisitorResumeAgainstRealBackend() {
        if (qEnvironmentVariableIntValue("DISK_DESKTOP_REAL_BACKEND") != 1) {
            QSKIP("Set DISK_DESKTOP_REAL_BACKEND=1 to run the real-backend scenario");
        }

        QNetworkAccessManager real_network;
        NetworkClient network_client(&real_network);
        const QString base_url = qEnvironmentVariable(
            "DISK_DESKTOP_BACKEND_URL",
            QStringLiteral("http://127.0.0.1:8080/")
        );
        network_client.SetBaseUrl(base_url);

        const QByteArray payload = make_payload();
        BackendFixture fixture(&network_client);
        QVERIFY2(fixture.Provision(payload), qPrintable(fixture.Failure()));

        QTemporaryDir temporary_dir;
        QVERIFY(temporary_dir.isValid());
        const QString target_path =
            temporary_dir.filePath(QStringLiteral("visitor-resume.bin"));
        QFile partial(target_path);
        QVERIFY(partial.open(QIODevice::WriteOnly | QIODevice::Truncate));
        QCOMPARE(partial.write(payload.first(PARTIAL_SIZE)), PARTIAL_SIZE);
        partial.close();

        write_resume_state(
            target_path,
            fixture.ShareId(),
            fixture.FileId(),
            static_cast<quint64>(payload.size()),
            static_cast<quint64>(PARTIAL_SIZE),
            fixture.PayloadHash()
        );
        const QString sidecar_path =
            target_path + QStringLiteral(".download.json");
        QVERIFY(QFileInfo::exists(sidecar_path));

        RequestFactory request_factory;
        request_factory.SetOwnerAccessToken(fixture.OwnerToken());
        request_factory.SetVisitorShareToken(fixture.ShareToken());
        TransferManager transfer_manager(&network_client, &request_factory);

        DownloadObservation observation;
        const QString expected_path =
            QStringLiteral("/api/share/download/%1/%2")
                .arg(fixture.ShareId(), QString::number(fixture.FileId()));
        const QByteArray expected_range =
            QStringLiteral("bytes=%1-").arg(PARTIAL_SIZE).toUtf8();
        QObject::connect(
            &real_network,
            &QNetworkAccessManager::finished,
            &transfer_manager,
            [&](QNetworkReply* reply) {
                if (reply->request().url().path() != expected_path) {
                    return;
                }
                const auto request = reply->request();
                ++observation.request_count;
                observation.status_code =
                    reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
                observation.range_matches =
                    request.rawHeader("Range") == expected_range;
                observation.visitor_token_matches =
                    request.rawHeader("X-Share-Token") ==
                    fixture.ShareToken().toUtf8();
                observation.authorization_absent =
                    request.rawHeader("Authorization").isEmpty();
            }
        );

        transfer_manager.StartShareDownload(
            fixture.ShareId(),
            fixture.FileId(),
            target_path,
            QStringLiteral("visitor-resume.bin"),
            static_cast<quint64>(payload.size()),
            fixture.PayloadHash()
        );

        QTRY_VERIFY_WITH_TIMEOUT(
            transfer_manager.GetDownloadModel()->rowCount() == 1,
            DOWNLOAD_TIMEOUT_MS
        );
        QTRY_VERIFY_WITH_TIMEOUT(
            [&]() {
                const auto task =
                    transfer_manager.GetDownloadModel()->GetTask(0);
                return task.has_value() &&
                       (task->status == QStringLiteral("completed") ||
                        task->status == QStringLiteral("failed"));
            }(),
            DOWNLOAD_TIMEOUT_MS
        );

        const auto task = transfer_manager.GetDownloadModel()->GetTask(0);
        QVERIFY(task.has_value());
        QCOMPARE(task->status, QStringLiteral("completed"));
        QCOMPARE(task->auth_domain, QStringLiteral("visitor"));
        QCOMPARE(task->transfer_mode, QStringLiteral("range"));
        QVERIFY(task->range_start.has_value());
        QCOMPARE(*task->range_start, static_cast<quint64>(PARTIAL_SIZE));
        QCOMPARE(task->received_bytes, static_cast<quint64>(payload.size()));
        QCOMPARE(task->local_partial_size, static_cast<quint64>(payload.size()));
        QCOMPARE(task->expected_size, static_cast<quint64>(payload.size()));
        QCOMPARE(task->verification_status, QStringLiteral("verified_hash"));

        QCOMPARE(observation.request_count, 1);
        QVERIFY(observation.range_matches);
        QVERIFY(observation.visitor_token_matches);
        QVERIFY(observation.authorization_absent);
        QCOMPARE(observation.status_code, 206);

        QFile completed(target_path);
        QVERIFY(completed.open(QIODevice::ReadOnly));
        const QByteArray completed_payload = completed.readAll();
        completed.close();
        QCOMPARE(completed_payload.size(), payload.size());
        QVERIFY(completed_payload == payload);
        QCOMPARE(md5(completed_payload), fixture.PayloadHash());
        QVERIFY(!QFileInfo::exists(sidecar_path));

        QVERIFY(QFile::remove(target_path));
        QVERIFY(!QFileInfo::exists(target_path));
        QVERIFY2(fixture.Cleanup(), qPrintable(fixture.CleanupFailure()));
    }
};

QTEST_GUILESS_MAIN(TestVisitorResumeIntegration)

#include "test_visitor_resume.moc"
