# Desktop engine release operations

This procedure publishes the engine installers. Game export uses [Building and Shipping](Building-and-Shipping.md).

The project owner explicitly approved **`v2026.1.0-beta.1` as an unsigned preview for early testers**. `scripts/release_policy.py` contains this exact exception. It does not extend to later betas, release candidates or stable versions. That first preview does not need signing accounts; its notes disclose Windows/macOS warnings or launch blocks and the outstanding manual acceptance. All CI, clean identity and checksum checks remain required. The signing setup below is for subsequent signed releases.

## Protect and select the source

Protect the default branch from deletion and force pushes. Require `windows-build`, `linux-build` and `ci-evidence` from the GitHub Actions integration. Validate a commit on `dev` before promoting that same commit to `master`. Protect existing `refs/tags/v*` tags from updates and deletion. Rulesets are repository settings, not checkout files.

The version in `Version.props`, the exact `CHANGELOG.md` heading and the planned `v<version>` tag must agree. Use the full commit identity in release records. Local uncommitted changes are not part of a CI artifact.

## Signing setup

### Windows: choose a supported signing provider first

The current Windows CI imports `FUNKE_SIGNING_CERTIFICATE_BASE64` as a PFX and reads its password from `FUNKE_SIGNING_CERTIFICATE_PASSWORD`. `FUNKE_SIGNING_TIMESTAMP_URL` is a repository variable for an RFC 3161 timestamp endpoint. The packaging scripts also support `-SigningCertificateThumbprint` with an accessible private key in the Windows certificate store, but the hosted CI currently selects the PFX path only.

This is not a complete onboarding solution for a new public certificate. Public code-signing keys are normally held in hardware or by a remote signing service; a certificate-only `.cer` is not an exportable PFX private key. Provider integration must cover the engine binaries, setup, Inno's uninstaller and the detached CMS checksum signature. Do not replace these requirements with a self-signed certificate or disable the release verification gates. See [Microsoft's signing options](https://learn.microsoft.com/en-us/windows/apps/package-and-deploy/code-signing-options) and [DigiCert's key storage requirements](https://knowledge.digicert.com/general-information/private-keys).

For a private individual in Germany, Azure Artifact Signing Public Trust is currently unavailable: individual enrollment is limited to the USA and Canada. An eligible organization has different regional availability. See the [Artifact Signing prerequisites](https://learn.microsoft.com/en-us/azure/artifact-signing/quickstart).

Two options to evaluate are [SignPath Foundation](https://signpath.org/apply) for eligible open-source projects and [Certum Code Signing](https://www.certum.eu/en/code-signing-certificates/) for individuals. SignPath acceptance is not automatic; its [conditions](https://signpath.org/terms) include an existing released project, a qualifying open-source license and release-process requirements. Certum offers individual identity verification and hardware/cloud signing. Confirm the unattended build integration before purchasing. Neither provider is integrated into this repository yet; their account credentials must not be entered into the existing PFX secrets.

### macOS: Developer ID and notarization

1. Enroll in the [Apple Developer Program](https://developer.apple.com/help/account/membership/program-enrollment) as an individual or organization and complete identity verification and the current agreements.
2. Create a **Developer ID Application** certificate using a certificate signing request. For this DMG workflow, use an exportable certificate and its matching private key, packaged as a password-protected `.p12`. The certificate-only download is insufficient. A Developer ID Installer certificate is for signed installer packages and is not the identity required here. See [Apple's certificate instructions](https://developer.apple.com/help/account/certificates/create-developer-id-certificates).
3. Create an App Store Connect **team API key** under Users and Access → Integrations → App Store Connect API. Use a role with notarization access. Preserve its `.p8` private key, Key ID and Issuer ID. The Issuer ID is not the Apple Team ID. The current helper passes `--issuer`, so provision a team key for this integration. See [Apple's API key setup](https://developer.apple.com/help/app-store-connect/get-started/app-store-connect-api).
4. Add these repository **Actions secrets** under Settings → Secrets and variables → Actions:

| Secret | Value |
| --- | --- |
| `MACOS_CERTIFICATE_P12_BASE64` | Base64 of the complete binary `.p12`, including its private key |
| `MACOS_CERTIFICATE_PASSWORD` | The `.p12` export password |
| `MACOS_SIGNING_IDENTITY` | Exact identity, such as `Developer ID Application: Legal Name (TEAMID)` |
| `APPLE_API_KEY_P8_BASE64` | Base64 of the downloaded `.p8` file |
| `APPLE_API_KEY_ID` | The API key's Key ID |
| `APPLE_API_ISSUER_ID` | The team API key's Issuer ID |

Base64 is an encoding, not encryption. Keep credential files and their encoded values outside the repository, release assets and logs. Configure all six together: partial configuration intentionally fails. `installer/macos/signing.py` prepares an isolated keychain and notarization profile; the build signs, notarizes, staples and verifies the app and DMG, and the cleanup step removes the temporary credentials. See [macOS installer development](../../../installer/macos/README.md).

## Preparation draft and signed tag handoff

An unsigned preparation draft may hold CI installer candidates, provided the notes and provenance explicitly identify their signing status. Keep it unpublished. Its source commit remains the commit recorded in those candidates, even if newer release automation is subsequently merged.

For a signed release, configure signing and complete the pre-tag acceptance first. The approved unsigned preview uses its explicit exception and discloses the remaining manual acceptance instead. Push the matching version tag at the reviewed `master` commit. The tag CI runs the complete matrix and the applicable signing gates. `prepare_github_release.py` then creates a draft or refreshes the existing draft for that version. It:

- verifies the remote tag points to the tested commit;
- requires the full artifact inventory and successful CI evidence from the same run attempt;
- checks installer identities, checksums and macOS signing/notarization metadata according to the exact tag policy;
- replaces candidate provenance and combined checksums with records for the new tag artifacts;
- refuses published releases and unexpected draft assets, including private symbols;
- verifies the uploaded asset sizes and GitHub SHA-256 digests before updating the release notes;
- leaves the result as an unpublished draft.

No preliminary draft deletion or tag replacement is needed. After a partial upload failure, correct the issue and rerun the job while the release remains a draft. Do not publish a draft during an upload. Unknown extra assets require review rather than automatic removal.

## Final acceptance and symbol retention

CI covers the installer smoke paths documented in the platform installer guides. It does not prove installation on a machine missing every prerequisite or replace the manual release acceptance. Record the exact installer hash, OS, machine identity, result and log location for each case:

- Clean Windows 10/11 x64 without .NET/VC++: prerequisite installation, UAC cancellation and retry, restart handling, install, C# authoring/export, upgrade and uninstall preserving projects.
- macOS on supported Apple Silicon: Gatekeeper acceptance of the notarized download, installation, C# authoring/export and launch of the exported game.
- Linux on the supported baseline: dependency installation, per-user installation, C# authoring/export and launch of the exported game.

Store each private symbol ZIP and its SHA-256 with the exact build ID in durable, access-controlled backup storage for the full supported release lifetime. A local working copy and GitHub's expiring CI artifacts are not that backup. Repeat this for the final signed build; an earlier candidate's symbols do not substitute for a later build's symbols.

Review the signed assets, changelog, included licenses, final acceptance and backup record before publishing the draft. No publication is performed by the CI helper.

For the approved first preview, review the unsigned artifacts and their known limitations before publishing as a pre-release. Keep the unresolved clean-machine cases explicitly pending; do not present the preview as production-qualified. Preserve the corresponding private symbols under the exact build ID in either case.
