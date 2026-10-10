# Installed public endpoint consumer

Configure this independent directory against a freshly installed SDK using
SouiSdk_DIR. It includes only public headers and links only Soui::Sdk; it does
not build another SDK or add a private provider source. Its smoke checks the new
factory symbol/layout, stopped ownership, policy freeze, invalid-options denial
and explicit stopped lifecycle. The six normal-SDK native composition cases
separately exercise actual pipe actions/tasks. Native V10 executed this consumer
against relocated installed SDKs on the current Windows host for both x64 and
Win32, with natural root0/supervisor0 and no forced cleanup. Records are
out/native-v10/x64/summary.json and out/native-v10/Win32/summary.json. This
consumer does not establish a live pipe or qualify another OS, long runs, GUI
integration or a release. A changed commit snapshot needs its own build/install
binding. Old headers/static libraries cannot be mixed with the new concrete
C++ layout.
