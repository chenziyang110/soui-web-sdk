"""Bind current linker library candidates to source notice *candidates* only."""

import hashlib
import json
import pathlib
import subprocess
import sys
from datetime import datetime, timezone

ROOT = pathlib.Path(r"C:\SOUI-SDK-development\preview14-scheduler")
EVIDENCE = pathlib.Path(r"C:\SOUI-SDK-production-validation\preview15-license-boundary-20260927")
OUTPUT = EVIDENCE / "current-notice-candidate-map-v1.json"

NOTICES = {
    "skia_gpu.lib": ["third_party/sources/miniblink/third_party/skia/LICENSE"],
    "skia.lib": ["third_party/sources/miniblink/third_party/skia/LICENSE"],
    "libcurl.lib": ["third_party/licenses/libcurl-7.61.1-COPYING.txt"],
    "zlib.lib": ["third_party/sources/miniblink/third_party/zlib/LICENSE"],
    "ots.lib": ["third_party/licenses/ots-5.0.0-LICENSE.txt"],
    "libxml.lib": ["third_party/sources/miniblink/third_party/libxml/src/Copyright"],
    "harfbuzz.lib": ["third_party/sources/miniblink/third_party/harfbuzz-ng/COPYING"],
    "v8_5_7.lib": ["third_party/sources/miniblink/v8_5_7/LICENSE"],
    "v8_5_7_1.lib": ["third_party/sources/miniblink/v8_5_7/LICENSE"],
    "libjpeg.lib": ["third_party/licenses/ijg-jpeg-9a-README.txt"],
    "libpng.lib": ["third_party/sources/miniblink/third_party/libpng/png.h"],
    "openssl.lib": ["third_party/sources/miniblink/node/openssl/openssl/LICENSE"],
}
ADDITIONAL = [
    "third_party/sources/miniblink/LICENSE",
    "third_party/sources/miniblink/third_party/brotli/LICENSE",
    "third_party/sources/miniblink/node/LICENSE-nodejs",
]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def read_json(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def notice_identity(path, entries):
    relative = pathlib.PurePosixPath(path)
    if str(relative) != path or path not in entries:
        raise ValueError(f"Notice absent from source manifest: {path}")
    indexed = subprocess.run(
        ["git", "show", f":{path}"], cwd=ROOT, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, check=True,
    ).stdout
    if sha(indexed) != entries[path]["sha256"] or len(indexed) != entries[path]["bytes"]:
        raise ValueError(f"Index and source manifest disagree: {path}")
    return {"path": path, "bytes": len(indexed), "sha256": sha(indexed)}


def main():
    if OUTPUT.exists():
        raise ValueError("Refusing to replace existing candidate map")
    candidate_path = EVIDENCE / "current-link-library-candidates.json"
    extraction_path = EVIDENCE / "current-link-command-extraction-v3.json"
    review_path = ROOT / "third_party/manifests/license-review.json"
    manifest_path = ROOT / "source-manifest.json"
    candidate = read_json(candidate_path)
    extraction = read_json(extraction_path)
    review = read_json(review_path)
    manifest = read_json(manifest_path)
    if (candidate["sourceReportSha256"] != sha(extraction_path.read_bytes())
            or candidate["actualResolvedLibraryFilesVerified"]
            or candidate["actualArchiveMembersRecovered"]
            or review["status"] != "unverified"
            or review["releaseReady"]
            or manifest["releaseReady"]):
        raise ValueError("Input identity or status mismatch")
    source_entries = {item["path"]: item for item in manifest["files"]}
    notice_paths = sorted(set(ADDITIONAL + [path for values in NOTICES.values()
                                            for path in values]))
    notices = {path: notice_identity(path, source_entries) for path in notice_paths}
    reviewed_binaries = {item["architecture"]: item["sha256"]
                         for item in review["binaries"]}
    extracted_binaries = {item["architecture"]: item["artifact"]["sha256"]
                          for item in extraction["binaries"]}
    if reviewed_binaries != extracted_binaries:
        raise ValueError("Current binary hashes do not match license-review")
    binaries = []
    for item in candidate["binaries"]:
        architecture = item["architecture"]
        if architecture not in reviewed_binaries:
            raise ValueError("Unexpected architecture")
        libraries = []
        mapped = 0
        for library in item["libraries"]:
            name = library["libraryName"].lower()
            local = library["candidatesInExplicitPaths"]
            if len(local) > 1 or library["ambiguousInExplicitPaths"]:
                raise ValueError(f"Ambiguous explicit path: {architecture} {name}")
            if local and name not in NOTICES:
                raise ValueError(f"Local candidate lacks notice lead: {architecture} {name}")
            leads = [notices[path] for path in NOTICES.get(name, [])] if local else []
            mapped += bool(leads)
            libraries.append({"name": name,
                              "candidateInExplicitLibPaths": bool(local),
                              "candidateSourceClass": (
                                  "local dependency build" if local and local[0]["underLocalDependencyBuild"]
                                  else "imported source" if local and local[0]["underImportedSource"]
                                  else "not located in explicit /LIBPATH"),
                              "noticeCandidates": leads})
        if mapped != item["candidateCount"] or len(libraries) != len(item["libraries"]):
            raise ValueError("Candidate count mismatch")
        binaries.append({"architecture": architecture,
                         "binarySha256": reviewed_binaries[architecture],
                         "explicitLibraryArguments": len(libraries),
                         "localCandidateNames": mapped,
                         "notLocatedInExplicitLibPaths": item["unresolvedCount"],
                         "libraries": libraries})
    report = {
        "schema": "soui.current-notice-candidate-map/v1",
        "generatedAtUtc": datetime.now(timezone.utc).isoformat(),
        "scope": "Current explicit linker library names mapped to candidate notice files from the indexed source manifest; neither actual library resolution nor archive member selection nor legal sufficiency is established",
        "inputs": {
            "candidateReportSha256": sha(candidate_path.read_bytes()),
            "linkExtractionSha256": sha(extraction_path.read_bytes()),
            "licenseReviewSha256": sha(review_path.read_bytes()),
            "sourceManifestSha256": sha(manifest_path.read_bytes()),
        },
        "binaries": binaries,
        "additionalNoticeLeads": [notices[path] for path in ADDITIONAL],
        "actualResolvedLibraryFilesVerified": False,
        "actualArchiveMembersRecovered": False,
        "nestedComponentInventoryComplete": False,
        "runtimeNoticeSetReviewed": False,
        "applicableRedistributionTermsReviewed": False,
        "status": "unverified",
        "releaseReady": False,
    }
    OUTPUT.write_text(json.dumps(report, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(json.dumps({"output": str(OUTPUT), "sha256": sha(OUTPUT.read_bytes()),
                      "x86Mapped": binaries[0]["localCandidateNames"],
                      "x64Mapped": binaries[1]["localCandidateNames"],
                      "status": "unverified"}))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
