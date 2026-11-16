---
dg-publish: true
her-note: false
---

```
+---.mcca.root
+---lib
+---apps
+---boot
|   \---grub
+---demo
|   +---mov
|   +---mzk
|   \---pic
+---drvs
\---font
```

### 串口快捷键

f/F 显示目录树
h/H 显示设备树
l 显示锁
m 内存使用信息
s 显示核调度状态以及所有线程
w 显示屏幕、窗体信息

TODO: 中断列表

LOGO+c 创建终端
LOGO+r 切换分辨率

### 调试配置

**网络** (QEMU, flap32)

```bash
# [TAP-Net]
#	-netdev tap,id=e1000net,ifname=tap0,script=no,downscript=no \
#	-device e1000,netdev=e1000net,mac=52:54:00:12:34:56 \
sudo ip tuntap add dev tap0 mode tap user $USER
sudo ip addr add 10.0.2.1/24 dev tap0
sudo ip link set tap0 up
sudo ip route replace 10.0.2.15/32 dev tap0
```

test:

```
testudp 10.0.2.1 7777 mecocoa
```




### 调试方式

- Specific kThread or Lock
- K-Assert
- Poison: Malloc 0xCC and Dealloc 0xDD
- Stack: 0xDEADBEEF
- Timeout:
	- Spinlock

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
