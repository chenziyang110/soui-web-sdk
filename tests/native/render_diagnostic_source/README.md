# Numeric render diagnostic regression SOURCE

Pending registration: target `soui_render_diagnostic_source_tests`, CTest
`render.diagnostic.source`. C++17; generated include directory; no separate
vendor .cpp linkage. Root owns shared CMake. The generator pins 3 sources / 3
copies: real wire header, numeric helper header, actual GpuSkiaSurface::present
body. No generator/build/test was run here.

Explicit recorder/OpenGL/context/SwapBuffers fakes exercise missing surface and
context, clean frames, disabled direct mode, blit and textured-quad repeated swap
failure followed by success. Native call count, dirty retention and successful
present accounting remain checked. Fixed diagnostic events must have all causal
fields zero and never register/inspect TLS. No filesystem APIs are supplied.
This proves neither actual GPU qualification nor pixels/T6/T7. Actual wheel
admission plus the same helper is separately bound in independent_wheel_source;
the C recorder SOURCE validates diagnostic enum and zero-relation rejection.
