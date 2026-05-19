#pragma once

#include <QString>

namespace disk::desktop::utils {

    class ShareCodeParser {
    public:
        static auto ParseShareInput(const QString& input) -> QString;
    };

} // namespace disk::desktop::utils
