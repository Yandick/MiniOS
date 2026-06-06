# MiniOS

MiniOS 是一个基于 **C11 + Linux/POSIX** 的用户态操作系统模拟平台。项目将进程管理、处理机调度、内存管理、页面置换、TinyFS 文件系统、进程同步、实时调度和状态分析统一到同一个 MiniOS Shell 中，并提供浏览器 GUI 和 Workbench 作为可视化演示入口。

它不是可启动的裸机内核，而是一个用于学习、实验和展示操作系统核心机制的完整模拟环境。用户可以在终端或浏览器 Workbench 中创建进程、推进 tick、观察 PCB 和 dmesg、操作 TinyFS、读写 fd、切换调度策略，并生成可复现的测试记录。

## Preview

![MiniOS homepage screenshot placeholder](demo/assets/main_page.png)

<!--
Replace the preview image above after capturing the real homepage or Workbench screenshot.

Suggested options:
- Keep the image in this repository, for example: demo/assets/readme-homepage.png
- Or use a GitHub uploaded image URL, for example: https://github.com/user-attachments/assets/...

Recommended screenshot targets:
- http://127.0.0.1:8000/
- http://127.0.0.1:8001/workbench.html
-->

## Demo Videos

Example Demo:

https://github.com/user-attachments/assets/5d20784e-9c71-4398-ba99-6edb24ec1cf5

Workbench Demo:

https://github.com/user-attachments/assets/9b2862a0-bd83-43dd-ba59-5a692a7aa64f
<!--
Replace the placeholder links above with the real demo video URLs after uploading your videos.

Recommended GitHub workflow:
1. Record an .mp4, .mov, or .webm demo video.
2. Upload it to a GitHub issue, pull request comment, or README edit box.
3. Copy the generated https://github.com/user-attachments/assets/... URL.
4. Replace REPLACE_WITH_EXAMPLE_DEMO_VIDEO and REPLACE_WITH_WORKBENCH_DEMO_VIDEO in the links above.

If you want GitHub to render the videos as inline media attachments, paste the generated URLs on their own lines below and remove this comment.

https://github.com/user-attachments/assets/REPLACE_WITH_EXAMPLE_DEMO_VIDEO
https://github.com/user-attachments/assets/REPLACE_WITH_WORKBENCH_DEMO_VIDEO
-->

## Features

| Module | What It Shows |
| --- | --- |
| MiniKernel | PCB table, tick scheduling, process states, BLOCKED/wakeup, MEM_WAIT, context switches, dmesg |
| Scheduling | FCFS, SJF, Priority, Round Robin, HRRN comparison, MLFQ with aging |
| Memory | First-fit dynamic partitioning, allocation/free, block merging, fragmentation statistics |
| Virtual Memory | Online frame table, FIFO/LRU replacement, page access history and fault rate comparison |
| Synchronization | Producer-consumer, readers-writers, dining philosophers with pthread mutex/semaphore |
| TinyFS | Directories, files, path operations, free block bitmap, save/load/fscheck image validation |
| File Descriptors | `open/readfd/writefd/seekfd/close`, offsets, modes, EOF, deleted path with opened fd |
| Realtime Scheduling | EDF/RMS periodic task simulation, job timeline, deadline miss and utilization |
| State Analysis | `tracebench`, `benchmark-subset`, `autotune`, CSV export and Markdown performance report |
| Web UI | MiniOS Control Center, Full Demo story page, browser Workbench connected to a real MiniOS Shell |

## Requirements

- Linux 或 WSL
- `gcc` with C11 support
- `make`
- `python3`
- POSIX threads support
- 浏览器，用于主页、Workbench 和 Demo 页

## Start Commands

只保留以下 4 个启动入口：

```bash
make home       # 启动本地服务并进入主页
make workbench  # 启动本地服务并进入 Workbench
make demo       # 启动本地服务并进入完整闭环 Demo 页
make tour       # 在本地终端运行默认 tour
```

服务启动后终端会打印对应 URL，例如：

```text
MiniOS URL: http://127.0.0.1:8001/workbench.html
Press Ctrl+C to stop the server.
```

## Pages

- 主页：`demo/index.html`
- Workbench：`demo/workbench.html`
- 完整闭环 Demo：`demo/minios_story.html?mode=full`

Workbench 会连接真实的 `build/os_project tour --interactive` 进程。浏览器里的命令会改变同一个 MiniKernel 状态，包括 PCB、tick、内存、VM、TinyFS、fd 表和 dmesg。

## MiniOS Shell

本地 tour 和 Workbench 终端都使用同一套 Shell。常用命令包括：

```text
help | about | overview              查看帮助和项目总览
status | ps | top                    查看内核状态、PCB、内存和文件系统
dmesg                                查看内核事件日志
pwd | cd | ls | tree | clear | echo  类 Unix 基础命令
create <name> <burst> <mem> [prio]   创建进程
tick [n] | run [n]                   推进调度 tick
sleep <pid> <ticks>                  阻塞进程并由 timer 唤醒
kill <pid>                           终止进程并回收内存
sched rr <q>|mlfq <q>|fcfs|sjf|priority
sched compare [q]                    基于当前 PCB 快照比较调度算法
mem                                  查看 first-fit 内存分区
access <pid> <page>                  记录在线 VM 页访问
vm [fifo|lru|reset|frames n]         配置在线 VM
page [frames]                        回放 FIFO/LRU 页面置换
mkdir | rmdir | touch | put | write  TinyFS 目录和文件操作
cat | rm | savefs | loadfs | fscheck TinyFS 读取、删除和镜像操作
open <path> [r|w|a|rw]               打开 TinyFS 文件
readfd | writefd | seekfd | close    文件描述符操作
fd | fds                             查看打开文件表
sync [all|pc|rw|dp]                  运行同步实验
realtime [time]                      运行 EDF/RMS 实时调度实验
exit | shutdown                      退出 MiniOS
```

## Project Layout

```text
.
├── include/os_project.h
├── src/
│   ├── main.c
│   ├── kernel.c
│   ├── scheduling.c
│   ├── memory.c
│   ├── sync_demo.c
│   ├── tinyfs.c
│   ├── realtime.c
│   ├── benchmark.c
│   └── utils.c
├── tests/test_core.c
├── data/
├── demo/
│   ├── index.html
│   ├── workbench.html
│   └── minios_story.html
├── scripts/
│   └── workbench_server.py
├── docs/
├── Makefile
└── SUBMISSION.md
```

