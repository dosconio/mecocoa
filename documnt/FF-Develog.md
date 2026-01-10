---
dg-publish: true
her-note: false
---

一些版本的编译器对 C、C++ 混合链接的支持不好。

...
✔️ 20251208 exit() wait() 
✔️ 20251207 实现新JMP-TSS过程：JmpTSS-P (consider Paging)
	现有的 x86 JMP TSS 机制，要求切换任务时新的页目录包含旧任务的TSS表的相同映射。（ア在メココア实践中发现的问题）
✔️ 20251206 fork() 
✔️ 20251204 之前的成果

---

# TODO

## x86

.
- [ ] 从裸机程式轉作業系統 —— exec 并激活 shell
- [ ] 动态加载中断例程/服务
- [ ] 外设实现 RuptTrait --> Mecocoa.DeviceTrait

內存管理（關鍵）
- [ ] memoman mempool （mempool需要自举）
- [ ] paging deep-copy heap
文件系統
- [ ] F_H -> H(h💿h💾) (RW 4个硬, 1个软)
- [ ] Debug all interfaces in the fs-trait : FAT32 Single-dir
- [ ] Support FAT32 Multilevel-dir
- [ ] MinixFS 与多FS动态绑定挂载与lsblk
- [ ] ofs/fatXX mkfs&writef&... 独立工具
系統接口
- [ ] syscall: yield, 非阻塞 getchar
介面渲染
- [ ] 4 Buffered VCon


# Reference

- UNISYM
- Teaching video of : https://github.com/StevenBaby/onix，學習內容如下
	- 037 时间
