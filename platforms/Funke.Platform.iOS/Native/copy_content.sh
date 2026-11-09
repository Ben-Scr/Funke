#!/bin/sh
set -eu
bundle="$TARGET_BUILD_DIR/$UNLOCALIZED_RESOURCES_FOLDER_PATH"
/usr/bin/ditto "$PROJECT_DIR/Content" "$bundle/game"
/usr/bin/ditto "$PROJECT_DIR/Managed" "$bundle/managed"
