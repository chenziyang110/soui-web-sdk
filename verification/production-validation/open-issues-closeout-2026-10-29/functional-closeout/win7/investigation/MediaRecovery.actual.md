# Recovered local Win7 media: read-only findings

The missing historical `D:/SOUI-SDK-QEMU-validation/system.qcow2` was not recovered. Two existing VMware Win7 **x64** disk sets were found under `F:/vm`; neither is the historical Win7 SP1 x86 V11 environment. The selected migration candidate is `F:/vm/Windows 7 x64 Sp1-000008.vmdk`. Its VM name says SP1; actual guest service pack, boot state, IME and application compatibility remain unobserved.

`MediaRecovery.actual.json` contains exact descriptor hashes, the 96 extent paths/header checks and before/after identities, sanitized occupancy, and actual qemu-img JSON. It contains no complete VMX, UNC path, physical-device path, catalog/lease values, guest files or media bytes.

## Verified source chain

| Descriptor suffix | CID | Parent CID | Extents |
|---|---|---|---:|
| `-000008.vmdk` | `2b05f511` | `930dac02` | 16 |
| `-000005.vmdk` | `930dac02` | `c12f6486` | 16 |
| `-000003.vmdk` | `c12f6486` | `edd2cdf4` | 16 |
| `-000002.vmdk` | `edd2cdf4` | `646d4d3d` | 16 |
| `-000001.vmdk` | `646d4d3d` | `4efc3352` | 16 |
| base `Windows 7 x64 Sp1.vmdk` | `4efc3352` | `ffffffff` | 16 |

All parent CID links match. All 96 named sparse extents exist, have valid VMDK magic and capacity matching their descriptors. Their aggregate physical length is 65,529,511,936 bytes. Virtual capacity is **64,424,509,440 bytes (60 GiB)**; the earlier exploratory messages calling it 64 GiB were incorrect. No full extent/disk hashing or guest filesystem scan was performed.

The selected leaf descriptor is 1,198 bytes, SHA-256 `5df33780835ff3190259ff402fec6c0c15c3d144395cae1556a3aef9f58b4f88`. The separate unselected `F:/vm/Windows 7 x64/Windows 7 x64.vmdk` is a complete standalone 16-extent x64 disk, but has no SP1 evidence and was not substituted for the selected leaf.

## Quiescence and trusted read-only tool

Immediately before the image operations, every visible VMware/QEMU/VirtualBox process command line was checked only for an exact `F:/vm/` reference. There were zero source references, zero unavailable command lines and zero source `.lck`/`.lock` entries. Raw command lines were omitted. This is a point-in-time observation; Root must recheck before conversion.

The qemu-img path came from the existing named `soui-devtools-windows` configuration. The executable is 1,966,340 bytes, SHA-256 `b4ef313ad2bd5070e44a5cccb50d8c2529dd68303c44ef8926b4c5a38ca219ff`; its bytes remained unchanged. Only these authorized commands ran, both returning 0:

```text
qemu-img info --backing-chain --output=json -f vmdk <selected-leaf>
qemu-img measure --output=json -f vmdk -O qcow2 <selected-leaf>
```

Info reports all six VMDK chain members. No `-U`, force-share, conversion, create, repair, pool mutation or VM startup occurred. Every descriptor hash and all 96 extent identities remained unchanged afterward.

## Conversion size and remaining firmware boundary

Measured qcow2 allocation: `required=38,598,868,992` bytes and `fully-allocated=64,434,601,984` bytes. The observed C: free space was 205,183,369,216 bytes. Root's conservative 64 GiB plus 20 GiB headroom budget is 90,194,313,216 bytes and fits C:. No output was allocated. Proposed destination remains `C:/SOUI-SDK-QEMU-Win7/base-win7-sp1-x64.qcow2`, guarded by a fresh directory/no-overwrite check and followed by separate fresh run overlays; F: is not the conversion destination.

The VMX declares `guestOS=windows7-64` and has no `firmware` or `efi.*` property. This provides a VMware default-BIOS **hint**, not an observed boot-firmware proof. The old log's BIOS disk geometry is also not sufficient to establish the boot firmware. Root must retain that distinction when selecting the supported VM profile.

The original SP1 VMX also enables a secondary raw host-disk attachment. It is outside the selected descriptor chain and must remain excluded. Do not import the full VMX or attach its secondary disk, floppy or inherited optical source. Only the selected system-disk leaf and its verified file-backed parents are migration inputs.

No installation ISO, licensing approval or activation state was established. Retained local disk presence is not redistribution approval. No credentials, EULA, full VMX or guest user data were inspected or copied. No compiler, product GUI, VM or conversion was started, and no production or Win7 acceptance result is claimed.
