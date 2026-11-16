#pragma once
#include "session.h"
#include "task.h"

namespace setup {
// Linux process transport. The caller validates the installation and holds Install.lock
// across this await. Kept separate so process tests need no proprietary key or game data.
task<int> runClientProcess(assistant::Launch launch, std::filesystem::path home);
}
