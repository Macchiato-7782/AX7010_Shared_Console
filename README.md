# AX7010 双屏共享终端

AX7010（XC7Z010-1CLG400），Vivado 2017.4，板上已验证的 ALINX Debian 8 启动镜像。HDMI 使用原厂 03 Debian 视频链路，小屏为 480×272。

## 日常使用

- HDMI 进 Xfce 后自动打开 Shared Terminal。关窗后可用 Alt+F2 输入 shared-console 再打开。
- HDMI 已连接时，键盘默认给桌面。按下并松开 Ctrl+Shift+F12 切到小屏，再按一次切回。
- PC 串口使用 115200 8N1，登录板上账号后自动接入同一个会话。串口软件使用支持 ANSI 的终端。
- 三端连接的是 tmux 套接字 shared 下的 shared 会话。输入、输出和当前目录共享。小屏的 58×15 网格决定共享内容的尺寸。
- 想离开共享会话时，按 Ctrl+B，再按 D。串口会回到原来的登录 Shell。输入 exit 会结束共享 Shell。

## 安装与回退

- 仓库只提交源码，不包含 FPGA 比特流、tmux 安装包和时序报告。
- 从 vivado 目录运行 vivado -mode batch -source build.tcl，生成 dual_console.bit 和 timing.rpt。
- 从 Debian Jessie 归档获取 tmux 1.9-6 armhf 包，保存为 board/tmux_1.9-6_armhf.deb。https://archive.debian.org/debian-archive/debian/pool/main/t/tmux/
- 把 dual_console.bit 放到 board 目录。先在目标板上装好上一版 A2 双屏包。复制整个 board 目录，进入该目录以 root 运行 sh install.sh，再重启。
- 回退运行 sh rollback.sh 后重启。离线可将 FAT 分区的 a2_fb43_top.bit.before-shared-console 复制到 a2_fb43_top.bit。
- tmux 1.9-6 armhf 包来自 Debian Jessie 归档，经 Packages 索引的 SHA256 校验。
- board/lcdterm 为预编译的 ARM 二进制，源码见 board/lcdterm.c。

## 启动页

AX7010 字标上方扫入，中间显示进度，四组走线随进度分段亮起。等待终端时进度上限为 95%。收到 Linux 小屏首帧信号后显示 100% 约 0.2 秒，再切到石墨工作台终端。约 30 秒未就绪时显示 Terminal not ready。

## 本机验收

2026-11-04 从 SD 卡重启验证。HDMI 以 1920×1080@60 输出，LCD 服务运行。实际 COM19、HDMI 终端 PTY、小屏终端 PTY 均连接到 tmux shared，会话输入、输出与目录变化同步，测试回显已从 LCD 帧缓冲逐字校验。现场确认小屏颜色恢复且彩边消失。LCD RGB 和同步信号同时寄存，9 MHz 输出时钟错相 90 度。布线后 WNS +0.510 ns，LCD 数据到 DCLK 的最小 setup 余量 +18.891 ns。原厂 HDMI SerialClk 约束警告保留。

## 许可证

MIT，见 LICENSE。vivado/ip_repo 下的 ALINX 和 Digilent IP 保留其原始许可。
