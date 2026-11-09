# Memory and resource lifetime verification

Recorded local result from 9 November 2026, transferred from the memory backlog
when consolidating the task files. This documentation change did not rerun tests.

The recorded Linux ASan/UBSan/LSan run passed 902 tests and 3,047,875 assertions,
with eight conditional/isolated cases skipped and exit code 0. The report states
no sanitizer findings and no suppressions. The exact environment and source
hashes are recorded in `bin-int/_agent/memory-linux-environment.log`; the test
output is `bin-int/_agent/memory-linux-sanitize-tests.log`.

This evidence applies only to that snapshot. Later tests added in parallel were
not part of the run, and the Windows suites had not been rerun after the Linux
fixes. Remote CI and coverage were not executed. There was no Windows Debug run
with global allocation interception and no completed long interactive Editor
soak test in this memory investigation.

Allocator retention at final shutdown, live cache plateaus, native CPU tracking,
managed heap growth and GPU memory are separate measurements. A successful
sanitizer suite does not establish a byte budget for retained allocator metadata
or a stable multi-hour application lifecycle. Unknown measurement domains must
remain explicitly unavailable rather than reported as zero.

Open work is maintained once, in the topic backlogs: native diagnostics and
retention under `Architektur_und_Speicher`, long-running performance under
`Performance`, and CI/coverage evidence under `CI_und_Tests` in
`.agents/backlogs/`. The archived memory backlog contained no remaining
implementation tickets; its unverified boundaries remain with those owners.
