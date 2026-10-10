# Installed UIA component consumer

This independent project selects an absolute `SOUI_CONSUMER_PREFIX`, requires the exact 0.5.0 config and imported `Soui::Uia`, and links its public header and real API. It never adds SDK source directories. The single native call verifies nullptr HWND rejection before COM or UIA loading; it does not establish provider availability or accessibility behavior.

Configure a fresh Windows/MSVC build against an SDK installed with `SOUI_BUILD_UIA=ON`, then build Release and run the executable and its `installed.uia.parameter-rejection` CTest. Repeat from a fresh build after moving the entire installed prefix. Requesting the same REQUIRED Uia component from a default OFF package must fail configuration. Preserve that actual negative result separately.

The two native/build inputs are exact copies of the historical x64/Win32 installed and relocated consumer that ran under Root supervision. Those histories retain compiler0/124 partials and their 23-source SDK binding. The later 24-source SDK, controlled Host startup adoption and final candidate require fresh verification; this source delivery does not grant release or full SDK qualification. See ADR-0047 for finite provider and installation boundaries.
