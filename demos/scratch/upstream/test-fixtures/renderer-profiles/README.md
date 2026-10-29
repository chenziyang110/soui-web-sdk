# Renderer profile test inputs

These are corresponding source fixtures from Scratch Editor commit
`b4b934aaf6641a539325a971249234f4a50a2f03`, licensed under the adjacent AGPL-3.0
`LICENSE`. `RenderWebGL.original.js` and `Silhouette.original.js` are unchanged
upstream sources. `runtime.with-counters.js` contains the existing three neutral
SOUI performance-counter additions produced by this SDK's build recipe. Its
SHA-256 is `c5ada9b1ed82bafc0a2ee301ff2c3096d20fe71e1db0cc6b912d70a775516230`.

`source-paths.json` is the sorted output of `git ls-files -z` at that same commit,
stored as a JSON array. Tests use it to ensure the new untracked cache helper is
included in corresponding source delivery. No generated bundle or source-build
receipt is included or synthesized by these tests.
