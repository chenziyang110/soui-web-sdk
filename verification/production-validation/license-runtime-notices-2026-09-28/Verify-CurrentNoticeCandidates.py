"""Read-only consistency check for the current notice lead map."""

import hashlib
import json
import pathlib
import subprocess
import sys

ROOT = pathlib.Path(r"C:\SOUI-SDK-development\preview14-scheduler")
BASE = pathlib.Path(r"C:\SOUI-SDK-production-validation\preview15-license-boundary-20260927")


def sha(data):
    return hashlib.sha256(data).hexdigest()


def load(path):
    return json.loads(path.read_text(encoding="utf-8-sig"))


def main():
    path = BASE / "current-notice-candidate-map-v1.json"
    report = load(path)
    candidate = load(BASE / "current-link-library-candidates.json")
    extraction = load(BASE / "current-link-command-extraction-v3.json")
    license_review = load(ROOT / "third_party/manifests/license-review.json")
    source = load(ROOT / "source-manifest.json")
    inputs = {
        "candidateReportSha256": BASE / "current-link-library-candidates.json",
        "linkExtractionSha256": BASE / "current-link-command-extraction-v3.json",
        "licenseReviewSha256": ROOT / "third_party/manifests/license-review.json",
        "sourceManifestSha256": ROOT / "source-manifest.json",
    }
    assert all(report["inputs"][key] == sha(file.read_bytes()) for key, file in inputs.items())
    assert candidate["sourceReportSha256"] == report["inputs"]["linkExtractionSha256"]
    assert report["schema"] == "soui.current-notice-candidate-map/v1"
    assert report["status"] == license_review["status"] == "unverified"
    assert not any(report[key] for key in (
        "actualResolvedLibraryFilesVerified", "actualArchiveMembersRecovered",
        "nestedComponentInventoryComplete", "runtimeNoticeSetReviewed",
        "applicableRedistributionTermsReviewed", "releaseReady"))
    assert len(report["binaries"]) == len(candidate["binaries"]) == len(extraction["binaries"]) == 2
    indexed = {item["path"]: item for item in source["files"]}
    notice_count = 0
    for record, original, built in zip(report["binaries"], candidate["binaries"], extraction["binaries"]):
        assert record["architecture"] == original["architecture"] == built["architecture"]
        assert record["binarySha256"] == built["artifact"]["sha256"]
        assert record["explicitLibraryArguments"] == len(original["libraries"])
        assert len(record["libraries"]) == len(original["libraries"])
        assert record["localCandidateNames"] == original["candidateCount"]
        assert record["notLocatedInExplicitLibPaths"] == original["unresolvedCount"]
        for mapped, source_library in zip(record["libraries"], original["libraries"]):
            assert mapped["name"] == source_library["libraryName"].lower()
            assert mapped["candidateInExplicitLibPaths"] == bool(source_library["candidatesInExplicitPaths"])
            if mapped["candidateInExplicitLibPaths"]:
                assert mapped["noticeCandidates"]
            else:
                assert not mapped["noticeCandidates"]
            notice_count += len(mapped["noticeCandidates"])
    notices = {item["path"]: item for item in report["additionalNoticeLeads"]}
    for binary in report["binaries"]:
        for library in binary["libraries"]:
            notices.update({item["path"]: item for item in library["noticeCandidates"]})
    for name, item in notices.items():
        assert indexed[name]["sha256"] == item["sha256"]
        assert indexed[name]["bytes"] == item["bytes"]
        blob = subprocess.run(["git", "show", f":{name}"], cwd=ROOT,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, check=True).stdout
        assert sha(blob) == item["sha256"] and len(blob) == item["bytes"]
    assert source["releaseReady"] is False
    print(json.dumps({"schema": "soui.current-notice-candidate-check/v1",
                      "reportSha256": sha(path.read_bytes()),
                      "libraries": [item["explicitLibraryArguments"] for item in report["binaries"]],
                      "mapped": [item["localCandidateNames"] for item in report["binaries"]],
                      "noticeReferences": notice_count,
                      "uniqueNoticeFiles": len(notices),
                      "status": "unverified", "releaseReady": False}))


if __name__ == "__main__":
    try:
        main()
    except Exception as error:
        print(f"Notice map consistency failure: {error}", file=sys.stderr)
        sys.exit(1)
