# preview.6 完整源码搬移与双架构编译

在提交 `a8b6114a` 的源码清单下，将 30,190 个文件、609,279,114 字节复制到仓库外目录 `D:\SOUI-SDK-QEMU-validation\preview6-source-stage-20260925-a8b6114a`。该目录没有 `.git`；其 `source-manifest.json` SHA-256 为 `DE312AA7051B5C9031DEC22AE5B4849766F314300BE8D1E18B216FAA515772B1`。[源清单校验](source-verify.json)在搬移后及构建后均通过，[工具链检查](doctor.json)通过。

从搬移目录重新打出 408 文件的 preview.6 [SDK 包](package-verify.json)，[源码与包匹配](package-source-match.json)；包清单 SHA-256 `E9310C4FB818275260B896049B5894D7EC0573CA4EF508C9A49C810D89028819`，与此前 preview.6 打包记录相同。再用该包在仓库外生成 `Preview6StageApp`，其锁定文件[校验通过](project-verify.json)。

| 工程 | x86 Release | x64 Release |
|---|---|---|
| 完整源码目录 | [构建报告](stage-x86-build.json)，build-id `dea3fa38-38d5-471a-91f8-824142308eae` | [构建报告](stage-x64-build.json)，build-id `93155ab5-f3af-44b4-99ea-59d99bcac18d` |
| 仓库外生成项目 | [构建报告](project-x86-build.json)，build-id `34071e35-171d-4b83-bb33-7b7928482102` | [构建报告](project-x64-build.json)，build-id `470e0363-9a72-4e43-a939-151d9cfee497` |

完整源码的两架构构建均记录 SDK/模板/Demo Web 与原生成功，源码构建指纹相同：`2ac1f9aded43c894b8d798fad2fffae233049eb79e29b4d8d447c68c056fe557`。生成项目两架构也完成 Web/原生编译；其工程输入指纹另为 `fab9eb44eeee729e5c345f36bc1b79ffcf7fb37d05449216337aa285f816eb3c`。原生构建日志和各报告的复制散列见 [artifact-hashes.json](artifact-hashes.json)。搬移源码两架构的[非桌面测试](stage-x86-unit.log) / [非桌面测试](stage-x64-unit.log)各有 SDK 包测试 14/14、Web/协议 59 项中 58 通过且 1 项因无调试器跳过、原生 CTest 6/6 通过。

这验证完整源码清单的搬移、SDK 包再生成和双架构应用**编译**；本轮未从搬移目录重编 Miniblink/SOUI 底层依赖，也未启动搬移目录或生成项目的 GUI 应用、执行 Debug/升级回退与目标设备场景。因此 `sourceDeliveryVerified=false`、`releaseReady=false`；发布前还需对最终源码快照重新生成清单和归档校验和。
