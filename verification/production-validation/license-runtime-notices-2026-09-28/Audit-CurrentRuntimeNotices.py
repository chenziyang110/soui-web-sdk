"""Observe the current runtime notice payload; do not infer legal clearance."""

import hashlib
import json
import sys
from datetime import datetime, timezone
from pathlib import Path


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    repo = Path(sys.argv[1]).resolve(strict=True)
    evidence = Path(__file__).resolve().parent
    review_path = repo / "third_party/manifests/license-review.json"
    link_path = evidence / "current-link-command-extraction-v3.json"
    source_license = repo / "third_party/sources/miniblink/node/openssl/openssl/LICENSE"
    review = json.loads(review_path.read_text(encoding="utf-8"))
    links = json.loads(link_path.read_text(encoding="utf-8"))
    assert review["status"] == "unverified" and review["releaseReady"] is False
    assert links["schema"] == "soui.current-kernel-link-argument-extraction/v3"
    assert len(review["binaries"]) == len(links["binaries"]) == 2
    license_bytes = source_license.read_bytes()
    assert b"Original SSLeay License" in license_bytes
    assert b"OpenSSL License" in license_bytes
    bins = []
    for arch in ("x86", "x64"):
        expected = next(x["sha256"] for x in review["binaries"] if x["architecture"] == arch)
        link = next(x for x in links["binaries"] if x["architecture"] == arch)
        assert link["artifact"]["sha256"] == expected
        names = link["linkCommand"]["explicitInputLibraryNames"]
        apps = []
        for app in ("SouiStarter", "SouiWorkbench"):
            root = repo / "out" / arch / "bin" / "Release" / app
            node = root / "node.dll"
            assert digest(node) == expected
            legal = root / "resources" / "legal"
            files = sorted(p for p in legal.rglob("*") if p.is_file())
            assert files and (legal / "THIRD_PARTY_NOTICES.md") in files
            entries = [{"path": p.relative_to(legal).as_posix(), "sha256": digest(p),
                        "bytes": p.stat().st_size} for p in files]
            apps.append({"app": app, "nodeDllSha256": expected,
                         "legalFiles": entries,
                         "exactSourceLicenseBytesPresent": any(p.read_bytes() == license_bytes for p in files),
                         "ssleayLicenseTextPresent": any(b"Original SSLeay License" in p.read_bytes() for p in files)})
        bins.append({"architecture": arch, "binarySha256": expected,
                     "explicitOpenSslLibArgument": "openssl.lib" in [n.lower() for n in names],
                     "apps": apps})
    assert bins[0]["explicitOpenSslLibArgument"]
    assert all(not app["exactSourceLicenseBytesPresent"] and not app["ssleayLicenseTextPresent"]
               for item in bins for app in item["apps"])
    report = {
        "schema": "soui.current-runtime-notice-observation/v1",
        "observedAtUtc": datetime.now(timezone.utc).isoformat(),
        "scope": "Exact current node.dll hashes, explicit linker arguments, source license, and four app-local legal directories only",
        "inputs": {"licenseReviewSha256": digest(review_path),
                   "linkExtractionSha256": digest(link_path),
                   "sourceOpenSslLicenseSha256": digest(source_license)},
        "sourceOpenSslLicensePath": source_license.relative_to(repo).as_posix(),
        "sourceLicenseContainsOpenSslAndSsLeayTexts": True,
        "officialVersionMatchedLicenseUrl": "https://github.com/openssl/openssl/blob/OpenSSL_1_0_2h/LICENSE",
        "binaries": bins,
        "observation": "x86 current link command explicitly names openssl.lib; none of the four current app-local legal directories contains the source OpenSSL/SSLeay license text",
        "limitations": ["Explicit input is not proof of selected archive members",
                        "x64 lack of explicit argument is not proof that OpenSSL is absent",
                        "Other source or online notices are not analyzed",
                        "No legal sufficiency or redistribution determination"],
        "licenseReviewStatus": "unverified", "releaseReady": False,
    }
    target = evidence / "current-runtime-notice-observation-v1.json"
    with target.open("x", encoding="utf-8", newline="\n") as out:
        json.dump(report, out, ensure_ascii=False, indent=2)
        out.write("\n")
    print(json.dumps({"report": str(target), "sha256": digest(target),
                      "apps": sum(len(item["apps"]) for item in bins)}))


if __name__ == "__main__":
    main()
