# 0048: Release version identity and acceptance remain separate

Status: candidate, 2026-10-09. No live source is changed by this candidate.

The SDK version is already 0.5.0-preview.1, but the independent acceptance
consumer still requires the 0.4.0 installed CMake package. Build reports,
optional GUI packaging and source SDK manifests also reject every RC and stable
version. These constraints prevent the planned RC and 0.5.0 delivery even when
the corresponding inputs have passed their separate acceptance gates.

Use one small validator for exact X.Y.Z, X.Y.Z-preview.N and X.Y.Z-rc.N
identities. Components are canonical, nonnegative safe integers. Ranges,
leading zeroes, build metadata, unregistered prerelease channels and strings
longer than 64 bytes are rejected. The manifest channel must match its exact
version: preview, rc or stable. Old preview manifests retain their format and
remain readable. A version continues to identify only one set of contents.

Reading an old optional GUI package, including verification after installation
or a downgrade, accepts only its complete previous preview source declaration
and outputs. That path additionally requires the previous GUI producer's exact
LF-normalized bytes (13693 bytes, SHA-256
26305269c19891cd5dfb6d781985114f7b6aa1d6437d22213d4cb6ba25c10ec3),
which do not depend on the new version helper. Both the declaration and tree
must omit that helper. A changed producer, changed source/output set, RC or
stable declaration cannot use this path. Every new GUI build and package still
binds sdk-version.cjs; an installed historical lock cannot be repackaged through
the compatibility verifier. The saved old producer is fixture data and is never
executed by the regression tests.

Build evidence and GUI metadata keep their existing exact version, input,
output, log and package consistency checks. Compiler continuation does not
change its supervision requirements or qualify a runtime. Source delivery
and installation still report releaseReady=false for all three channels;
formal publication continues to require the external, current-payload release
acceptance checks. A stable version string alone never authorizes publication.

Update the current independent consumer to find_package(SouiSdk 0.5.0 EXACT).
The package's CMake numeric version remains 0.5.0 while the SDK header and
JavaScript package metadata preserve the complete preview or RC identity.
Historical 0.4.0 records and frozen deliveries are untouched. A consumer of a
different SDK line must explicitly request that line.

Verification covers canonical versions, manifest channel mismatch, pack and
installation across preview/RC/stable, rollback and the same-version/different-
contents rejection. Existing GUI and build supervision fixtures are extended
to exercise RC/stable without changing their evidence requirements. These are
finite filesystem/contract tests, not native runtime or production acceptance.
