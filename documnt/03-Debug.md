---
dg-publish: true
her-note: false
---

### 串口快捷键

h/H 显示设备树
l 显示锁
s 显示核调度状态以及所有线程
f/F 显示目录树

### 方式

- K-Assert


### 高频问题

- 有没有可能是哪里申请内存没有replacement new 导致有垃圾值
- 栈太小
- Rectangle() {} 是空构造函数，没有清零初始化

### GPT

```
truncate -s 520M disk_pata01.vhd
chmod 644 /home/phina/mcca/part_pata01/*.conf
systemd-repart --empty=allow --definitions=part_pata01 --dry-run=no disk_pata01.vhd
```
