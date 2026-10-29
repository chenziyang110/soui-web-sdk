# Existing second Win7 asset: bounded read-only gate

Verdict: plausible standalone legacy Win7 x64 disk; **SP1 and a usable environment are unproven**. Stop this investigation without conversion or VM creation. Root owns any later environment decision.

`F:/vm/Windows 7 x64/Windows 7 x64.vmdk` matches its existing1127B descriptor pin, has parentCID=ffffffff and sixteen complete local splitSparse extents,60GiB virtual. Fresh unchanged native/targeted occupancy checks passed. Exactly17 source files were held with FileAccess.Read/FileShare.Read through the one `qemu-io -r -f vmdk ... -c "read -v 0 512"` command and unchanged length/mtime proof, then all17 handles released. Native command and host script exited0. File IDs were not claimed from PowerShell.

The512-byte MBR has55aa and one active type07 NTFS partition, startLBA2048/count125825024, with three empty entries. No protective GPT entry appears; GPT headers and OS files were not read. This is plausible legacy boot metadata, not proof of bootability or installed OS/SP level.

The original VMX declares guestOS=windows7-64 and primary scsi0:0/controller lsisas1068 enabled. Its declared GBK encoding was respected. No explicit firmware/boot order or historical GuestInfo fields exist, so7601/SP1 cannot be established. One secondary UNC-backed device was detected; its values were not recorded/opened. Do not import the full VMX or attach that secondary. Physical-backing absence is not asserted. NVRAM inspection was limited to its32-byte header.

No conversion, VM/register/start, ISO mount/copy, OS-file/MFT or credential read, source/base/VMX write, network access, service/permission change or Git operation occurred. The first source's failed boot evidence, read-only base and writable overlay remain separate and preserved.
