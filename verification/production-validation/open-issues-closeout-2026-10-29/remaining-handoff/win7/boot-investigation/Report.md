# Win7 boot prerequisite: existing repair media

Existing SOUI-owned Microsoft Server 2022 x64 evaluation installation media is available at:

`C:/Users/11034/.codex/task-tools/qemu-ucrt64-11.1.2-e89cf96f2d1e4685bbdac347cb9eea3d/SERVER_EVAL_x64FRE_en-us.iso`

It is 5,044,094,976 bytes. The task original full-EOF download/inspection records bind SHA256 `3e4fa6d8507b554856fc9ca6079cc402df11a8b79344871669f0251535255325`. The resume completed with native exit0 and a verified Content-Range, matching the prior root download. Recorded source is Microsoft's evaluation-center page and Microsoft download host. A vendor-published digest/signature was not claimed. Four original task ISO-input receipts agree. The current path/length/file identity were observed; this investigation did not rehash the5GB ISO, so its current bytes are not claimed proven by metadata alone.

The existing WIM metadata identifies four Windows Server2022 x64 evaluation images, version10.0.20348.587. The original installer command used `pc,accel=tcg`, `bios-256k.bin` and `-boot order=cd,once=d`. These are historical metadata, not a new Win7 VM boot result. No ISO was mounted, copied or extracted here.

The original Win7 system disk is exactly `scsi0:0.fileName=Windows 7 x64 Sp1-000008.vmdk`, with `scsi0.virtualDev=lsisas1068` (LSI SAS), both controller and device present. No explicit firmware/BIOS/EFI boot-order property appears in that VMX. This does not prove the no-OS message's cause. Full VMX, unrelated devices, raw physical-secondary and UNC values were not republished or attached.

Root can use the existing Microsoft setup/WinPE media to inspect only the new writable overlay for actual Windows OS files and boot partition state before selecting a repair. Do not modify the read-only converted base or original VMDKs, and never attach the source VM's physical secondary. No VM input, media change, guest-userdata read, repair, service/credential/security change, download, install or Git action occurred in this investigation. Original three Win7 reports remain untouched.
