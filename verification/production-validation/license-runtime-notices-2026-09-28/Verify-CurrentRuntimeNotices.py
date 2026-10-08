"""Independently recheck the current-runtime notice observation against files."""

import hashlib
import json
import sys
from pathlib import Path


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    repo = Path(sys.argv[1]).resolve(strict=True)
    root = Path(__file__).resolve().parent
    report_path = root / "current-runtime-notice-observation-v1.json"
    report = json.loads(report_path.read_text(encoding="utf-8"))
    review_path = repo / "third_party/manifests/license-review.json"
    links_path = root / "current-link-command-extraction-v3.json"
    review = json.loads(review_path.read_text(encoding="utf-8"))
    links = json.loads(links_path.read_text(encoding="utf-8"))
    assert report["schema"] == "soui.current-runtime-notice-observation/v1"
    assert report["licenseReviewStatus"] == review["status"] == "unverified"
    assert report["releaseReady"] is review["releaseReady"] is False
    for name, path in (("licenseReviewSha256", review_path),
                       ("linkExtractionSha256", links_path)):
        assert report["inputs"][name] == sha(path)
    license_path = repo / report["sourceOpenSslLicensePath"]
    license_bytes = license_path.read_bytes()
    assert report["inputs"]["sourceOpenSslLicenseSha256"] == sha(license_path)
    assert b"Original SSLeay License" in license_bytes
    assert b"OpenSSL License" in license_bytes
    assert len(report["binaries"]) == 2
    checked = 0
    for item in report["binaries"]:
        arch = item["architecture"]
        assert arch in ("x86", "x64")
        review_bin = next(b for b in review["binaries"] if b["architecture"] == arch)
        link = next(b for b in links["binaries"] if b["architecture"] == arch)
        binary_sha = review_bin["sha256"]
        assert item["binarySha256"] == link["artifact"]["sha256"] == binary_sha
        names = [x.lower() for x in link["linkCommand"]["explicitInputLibraryNames"]]
        assert item["explicitOpenSslLibArgument"] is ("openssl.lib" in names)
        assert len(item["apps"]) == 2
        for app in item["apps"]:
            name = app["app"]
            assert name in ("SouiStarter", "SouiWorkbench")
            directory = repo / "out" / arch / "bin" / "Release" / name
            assert app["nodeDllSha256"] == sha(directory / "node.dll") == binary_sha
            legal = directory / "resources/legal"
            actual = {p.relative_to(legal).as_posix(): p for p in legal.rglob("*") if p.is_file()}
            recorded = {row["path"]: row for row in app["legalFiles"]}
            assert actual.keys() == recorded.keys()
            assert len(actual) == 5
            assert "THIRD_PARTY_NOTICES.md" in actual
            for relative, file in actual.items():
                row = recorded[relative]
                assert row["sha256"] == sha(file)
                assert row["bytes"] == file.stat().st_size
            assert app["exactSourceLicenseBytesPresent"] is False
            assert app["ssleayLicenseTextPresent"] is False
            assert all(p.read_bytes() != license_bytes and
                       b"Original SSLeay License" not in p.read_bytes()
                       for p in actual.values())
            checked += 1
    assert {x["architecture"] for x in report["binaries"]} == {"x86", "x64"}
    assert all({a["app"] for a in x["apps"]} == {"SouiStarter", "SouiWorkbench"}
               for x in report["binaries"])
    assert next(x for x in report["binaries"] if x["architecture"] == "x86")["explicitOpenSslLibArgument"]
    print(json.dumps({"verifiedApps": checked, "reportSha256": sha(report_path),
                      "licenseReviewStatus": "unverified", "releaseReady": False}))


if __name__ == "__main__":
    main()
