#ifndef FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_FILE_SAVE_H
#define FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_FILE_SAVE_H

#include <memory>
#include <string>

#include "fusiondesk/modules/clipboard/clipboard_types.h"

namespace fusiondesk {
namespace session {
class Session;
} // namespace session

namespace apps {
namespace pc {

bool clipboardSaveFilesRequested(int argc, char** argv);

bool saveClipboardRemoteFiles(
    int argc,
    char** argv,
    session::Session& session,
    const std::shared_ptr<modules::clipboard::IClipboardRemoteReader>&
        remoteReader,
    std::string* errorMessage = nullptr);

} // namespace pc
} // namespace apps
} // namespace fusiondesk

#endif // FUSIONDESK_APPS_PC_COMMON_PC_CLIPBOARD_FILE_SAVE_H
