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

`sprite-selector-item.original.jsx` and `sprite-list.original.jsx` are the
unchanged GUI sources at the same pinned commit. Their LF SHA-256 values are
`991850e8290374988e65ed5eb3d99dd419cbf1ecae4de965c864deea9bad8e09`
and `fc3800b42fddeb2ed1aadd0c621ddc6f6162e56d28a4746771aad69f1b653a39`.
The copies were checked against the corresponding-source inventory of the
previously executed `local-hull-cache-v1` source build. They retain the adjacent
AGPL-3.0 license and protect the new GUI profile's before-patch boundary.
