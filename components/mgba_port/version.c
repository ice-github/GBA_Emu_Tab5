/* Pinned version stub replacing upstream-generated src/core/version.c.
 * Upstream fills these from git at configure time; this port pins the
 * vendored release so firmware builds never depend on submodule git state.
 */
#include <mgba/core/version.h>

MGBA_EXPORT const char* const gitCommit = "26b7884bc25a5933960f3cdcd98bac1ae14d42e2";
MGBA_EXPORT const char* const gitCommitShort = "26b7884b";
MGBA_EXPORT const char* const gitBranch = "0.10.5";
MGBA_EXPORT const int gitRevision = 0;
MGBA_EXPORT const char* const binaryName = "gba-tab5";
MGBA_EXPORT const char* const projectName = "mGBA";
MGBA_EXPORT const char* const projectVersion = "0.10.5";
