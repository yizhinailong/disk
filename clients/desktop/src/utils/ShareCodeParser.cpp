#include "utils/ShareCodeParser.hpp"

#include <QRegularExpression>
#include <QString>

namespace disk::desktop::utils {

    auto ShareCodeParser::ParseShareInput(const QString& input) -> QString {
        auto trimmed = input.trimmed();
        if (trimmed.isEmpty()) {
            return {};
        }

        // If input contains "/s/", extract the segment after the last "/s/".
        auto last_pos = trimmed.lastIndexOf("/s/");
        if (last_pos >= 0) {
            auto code_segment = trimmed.mid(last_pos + 3);

            // Strip any trailing path components after the code.
            auto slash_pos = code_segment.indexOf('/');
            if (slash_pos >= 0) {
                code_segment = code_segment.left(slash_pos);
            }

            // Remove query string or fragment if present.
            auto query_pos = code_segment.indexOf('?');
            if (query_pos >= 0) {
                code_segment = code_segment.left(query_pos);
            }
            auto fragment_pos = code_segment.indexOf('#');
            if (fragment_pos >= 0) {
                code_segment = code_segment.left(fragment_pos);
            }

            code_segment = code_segment.trimmed();
            if (code_segment.isEmpty()) {
                return {};
            }

            // Validate extracted code: 8 alphanumeric characters.
            static const QRegularExpression re("^[a-zA-Z0-9]{8}$");
            auto match = re.match(code_segment);
            if (match.hasMatch()) {
                return code_segment;
            }
            return {};
        }

        // No "/s/" found: validate as raw share code.
        static const QRegularExpression raw_re("^[a-zA-Z0-9]{8}$");
        auto raw_match = raw_re.match(trimmed);
        if (raw_match.hasMatch()) {
            return trimmed;
        }

        return {};
    }

} // namespace disk::desktop::utils
