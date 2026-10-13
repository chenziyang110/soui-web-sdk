# Kernel notice materials

This index locates original declaration files for the twelve D52 source/build contexts. It is a material inventory, not a determination of applicable terms or approval to distribute a complete binary. The inherited preview.14 binary review remains `unverified` and `releaseReady=false`; its old hashes do not identify a new D52 binary.

| Build context | Original source declaration | Retained material |
|---|---|---|
| zlib | `third_party/zlib/LICENSE` | [Original LICENSE](d52/zlib/LICENSE.txt) |
| libjpeg | `third_party/libjpeg/jpeglib.h` points to README | [Recovered original IJG 9a README](ijg-jpeg-9a-README.txt) |
| libpng | `third_party/libpng/png.h` includes the license | [Complete original png.h](d52/libpng/png.h) |
| HarfBuzz | `third_party/harfbuzz-ng/COPYING` | [Original COPYING](d52/harfbuzz/COPYING.txt) |
| libxml | `third_party/libxml/src/Copyright` | [Original Copyright](d52/libxml/Copyright.txt) |
| OTS | `third_party/ots/include/opentype-sanitiser.h` points to LICENSE | [Recovered original OTS 5.0.0 LICENSE](ots-5.0.0-LICENSE.txt) |
| OpenSSL | `node/openssl/openssl/LICENSE` | [Complete original OpenSSL/SSLeay document](d52/openssl/LICENSE.txt) |
| libcurl | `third_party/libcurl/include/curl/curl.h` points to COPYING | [Recovered original curl 7.61.1 COPYING](libcurl-7.61.1-COPYING.txt) |
| V8 v8_5_7 | `v8_5_7/LICENSE` | [Original LICENSE](d52/v8_5_7/LICENSE) |
| V8 v8_5_7_1 | The same `v8_5_7` source family, a second build project | [Same original LICENSE](d52/v8_5_7/LICENSE) |
| Skia | `third_party/skia/LICENSE` | [Original LICENSE](d52/skia/LICENSE.txt) |
| Miniblink Main | Project root LICENSE and separate WKE header declaration | [Root LICENSE](d52/miniblink/LICENSE.txt), [original WKE declaration](d52/miniblink/wke-license-declaration.h) |

The recovered IJG, curl and OTS texts were already present in this SDK's license directory. The original recovery record documents version matching and local source differences; it does not establish whole-binary applicability. Source paths above identify the declaration origin, not a promise that the entire kernel source tree is contained in the portable SDK.

Adjacent and aggregate original texts are also retained:

- V8: [LICENSE.v8](d52/v8_5_7/LICENSE.v8), [LICENSE.fdlibm](d52/v8_5_7/LICENSE.fdlibm), [LICENSE.strongtalk](d52/v8_5_7/LICENSE.strongtalk), [LICENSE.valgrind](d52/v8_5_7/LICENSE.valgrind).
- OTS subsidiary material: [Brotli LICENSE](d52/brotli/LICENSE.txt).
- Node aggregate (including a c-ares declaration): [LICENSE-nodejs](d52/node/LICENSE-nodejs.txt).
- WebKit: [LICENSE](d52/webkit/LICENSE), [LICENSE_FOR_ABOUT_CREDITS](d52/webkit/LICENSE_FOR_ABOUT_CREDITS), [LICENSE-APPLE](d52/webkit/core/LICENSE-APPLE), [LICENSE-LGPL-2](d52/webkit/core/LICENSE-LGPL-2), [LICENSE-LGPL-2.1](d52/webkit/core/LICENSE-LGPL-2.1).

These different project, file and subsidiary declarations remain separate materials. Actual member selection, configuration, complete runtime payload and applicable redistribution obligations require review against each final architecture-specific binary. No single root LICENSE or API-header declaration covers that review.

The source SDK retains the eight original historical evidence files under `third_party/manifests/provenance/preview14/`, followed by their original repository-relative paths. They are archived preview evidence, not a new D52 build or license approval. Their original prose, old hashes and repository-relative links are preserved as history; those embedded links are not asserted to resolve as a complete portable repository or application resource tree. Applications carry the declaration texts and this index; they do not carry the archived 5 MB historical source manifest and patches.
