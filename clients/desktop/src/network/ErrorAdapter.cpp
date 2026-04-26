/**
 * @file ErrorAdapter.cpp
 * @brief Error mapping implementation from doc 02 §6.3
 *
 * @copyright Copyright (c) 2026
 */

#include "network/ErrorAdapter.hpp"

namespace disk::desktop {

    auto ErrorAdapter::FromJson(const QJsonObject& json) -> ApiError {
        ApiError err;
        err.code = json.value("code").toInt(0);
        err.family = Classify(err.code);
        err.category = MapToCategory(err.code);
        err.message = json.value("message").toString();
        err.retryable = IsRetryable(err.code);
        err.action = MapToAction(err.code);

        if (json.contains("field")) {
            err.field = json.value("field").toString();
        }
        if (json.contains("value")) {
            err.value = json.value("value").toString();
        }

        return err;
    }

    auto ErrorAdapter::FromNetworkError(QNetworkReply::NetworkError error) -> ApiError {
        ApiError err;
        err.family = "network";
        err.retryable = false;

        switch (error) {
            case QNetworkReply::ConnectionRefusedError:
                err.code = -1;
                err.category = "ConnectionFailed";
                err.message = "Connection refused";
                err.action = "retry_with_backoff";
                err.retryable = true;
                break;
            case QNetworkReply::RemoteHostClosedError:
                err.code = -2;
                err.category = "ConnectionFailed";
                err.message = "Remote host closed connection";
                err.action = "retry_with_backoff";
                err.retryable = true;
                break;
            case QNetworkReply::TimeoutError:
                err.code = -3;
                err.category = "Timeout";
                err.message = "Request timed out";
                err.action = "retry_with_backoff";
                err.retryable = true;
                break;
            case QNetworkReply::SslHandshakeFailedError:
                err.code = -4;
                err.category = "TlsError";
                err.message = "TLS handshake failed";
                err.action = "check_network";
                break;
            case QNetworkReply::HostNotFoundError:
                err.code = -5;
                err.category = "DnsError";
                err.message = "Host not found";
                err.action = "check_network";
                break;
            default:
                err.code = -99;
                err.category = "NetworkError";
                err.message = "Network error";
                err.action = "retry_with_backoff";
                err.retryable = true;
                break;
        }

        return err;
    }

    auto ErrorAdapter::Classify(int code) -> QString {
        if (code >= 10001 && code <= 10999) {
            return "general";
        }
        if (code >= 40001 && code <= 40999) {
            return "auth";
        }
        if (code >= 50001 && code <= 50999) {
            return "file";
        }
        if (code >= 60001 && code <= 60999) {
            return "share";
        }
        if (code >= 70001 && code <= 70999) {
            return "redis";
        }
        return "unknown";
    }

    auto ErrorAdapter::MapToCategory(int code) -> QString {
        switch (code) {
            case 10001: return "ValidationError";
            case 10002: return "ValidationError";
            case 10003: return "NotFound";
            case 10004: return "Conflict";
            case 10005: return "RateLimited";
            case 10006: return "ServerFailure";
            case 40001: return "IdentityConflict";
            case 40002: return "IdentityConflict";
            case 40100: return "NotFound";
            case 40101: return "CredentialsRejected";
            case 40102: return "AccountRestricted";
            case 40103: return "AccountRestricted";
            case 40104: return "SessionExpired";
            case 40105: return "ReLoginRequired";
            case 40106: return "AuthProtocolError";
            case 40107: return "AuthProtocolError";
            case 40108: return "SessionExpired";
            case 40109: return "AuthProtocolError";
            case 40110: return "ReLoginRequired";
            case 40111: return "ReLoginRequired";
            case 50001: return "FileConstraint";
            case 50002: return "FileConstraint";
            case 50003: return "FileConstraint";
            case 50004: return "StorageQuotaExceeded";
            case 50005: return "NotFound";
            case 50006: return "NotFound";
            case 50007: return "Conflict";
            case 50008: return "UploadSessionExpired";
            case 50009: return "TransferIntegrityError";
            case 50010: return "Conflict";
            case 50011: return "TransferReadError";
            case 60001: return "ShareUnavailable";
            case 60002: return "ShareExpired";
            case 60003: return "SharePasswordRejected";
            case 60004: return "PermissionDenied";
            case 70001: return "InfrastructureError";
            case 70002: return "InfrastructureError";
            case 70003: return "RemoteStateMissing";
            default   : {
                QString fam = Classify(code);
                if (fam == "general") {
                    return "RequestError";
                }
                if (fam == "auth") {
                    return "AuthenticationError";
                }
                if (fam == "file") {
                    return "FileDomainError";
                }
                if (fam == "share") {
                    return "ShareDomainError";
                }
                if (fam == "redis") {
                    return "InfrastructureError";
                }
                return "UnknownError";
            }
        }
    }

    auto ErrorAdapter::MapToAction(int code) -> QString {
        switch (code) {
            case 10001: return "fix_request";
            case 10002: return "fix_request";
            case 10003: return "refresh_context";
            case 10004: return "refresh_then_retry";
            case 10005: return "wait_and_retry";
            case 10006: return "retry_or_report";
            case 40001: return "change_input";
            case 40002: return "change_input";
            case 40101: return "reenter_credentials";
            case 40102: return "wait_or_contact_support";
            case 40103: return "contact_support";
            case 40104: return "refresh_owner_session";
            case 40105: return "clear_session_and_login";
            case 40106: return "rebuild_request";
            case 40107: return "clear_session_and_login";
            case 40108: return "refresh_owner_session_or_reverify_share";
            case 40109: return "switch_auth_domain";
            case 40110: return "clear_session_and_login";
            case 40111: return "clear_session_and_login";
            case 50001: return "change_input";
            case 50002: return "change_input";
            case 50003: return "change_input";
            case 50004: return "show_storage_and_stop";
            case 50005: return "refresh_context";
            case 50006: return "refresh_context";
            case 50007: return "rename_or_choose_target";
            case 50008: return "restart_upload";
            case 50009: return "retry_chunk_or_restart";
            case 50010: return "rename_or_choose_target";
            case 50011: return "retry_download";
            case 60001: return "close_share_entry";
            case 60002: return "close_share_entry";
            case 60003: return "reenter_share_password";
            case 60004: return "disable_download";
            case 70001: return "retry_with_backoff";
            case 70002: return "retry_with_backoff";
            case 70003: return "revalidate_session_or_refresh";
            default   : return "report_error";
        }
    }

    auto ErrorAdapter::IsRetryable(int code) -> bool {
        switch (code) {
            case 10005: return true;
            case 10006: return true;
            case 40104: return true;
            case 40108: return true;
            case 50008: return true;
            case 50009: return true;
            case 50011: return true;
            case 70001: return true;
            case 70002: return true;
            case 70003: return true;
            default   : return false;
        }
    }

} // namespace disk::desktop
