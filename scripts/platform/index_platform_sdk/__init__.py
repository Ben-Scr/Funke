"""Index Platform Builder SDK.

Shared, engine-owned helpers for platform packages (platforms/Index.Platform.*). A platform package
ships a builder script that the editor runs with one of three commands:

    <builder> describe [--engine-root DIR]          -> one JSON object on stdout
    <builder> check    [--engine-root DIR]          -> one JSON object {ok, issues[]}
    <builder> build    --request request.json       -> JSON lines (progress/log), then a final result

The request file and every event are documented in protocol.py. Builders import this SDK from
<engine root>/scripts/platform, which the editor passes as --engine-root; the SDK version is
protocol.PROTOCOL_VERSION so packages can reject an engine that is too old.
"""

from .protocol import (  # noqa: F401
    PROTOCOL_VERSION,
    BuildRequest,
    BuilderError,
    emit_log,
    emit_progress,
    emit_result,
    run_builder,
)
from .cache import cache_root, platform_cache  # noqa: F401
from .tools import find_cmake, find_ninja, run, which  # noqa: F401
