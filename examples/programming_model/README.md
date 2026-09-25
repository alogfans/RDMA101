# RDMA 编程模型实验

这三个程序配合教程第二篇使用。先查询设备，再观察资源的创建与销毁，最后让同一进程内的两个 RC QP 通信。实验仍需要可用的 RDMA 设备或 RXE；单进程通信不能代表跨机性能。

## 编译与运行

在仓库根目录执行：

```bash
make -C examples/programming_model
./examples/programming_model/01_device_info
./examples/programming_model/02_resource_lifecycle
./examples/programming_model/03_rc_loopback
```

`01_device_info` 输出设备能力、端口和 GID 信息。`02_resource_lifecycle` 创建 Context、PD、CQ、QP 与 MR，观察 QP 进入 INIT 后再释放资源，不进行数据传输。`03_rc_loopback` 执行 SEND、WRITE、READ，并在设备报告支持 Atomic 时执行 CAS。

RC 通信程序的 SEND 阶段应观察到发送和接收两个完成；WRITE 与 READ 阶段各观察发起端完成。用 `wr_id` 对应请求，用状态判断是否成功。示例输出将完成标为 CQE，应用通过 Verbs 实际取得的是 WC；其中 `byte_len` 的有效含义随操作而异，不能把每行的该字段都当作传输长度。

## 选择设备与实验参数

三个程序都支持 `-d` 选择设备、`-p` 选择端口，端口默认为 1。`01_device_info` 和 `03_rc_loopback` 还支持 `-g`，GID index 默认为 0。下面用 `mlx5_0`、端口 1、GID index 3 示意显式选择；这些值应以本机查询结果为准。

```bash
./examples/programming_model/01_device_info -d mlx5_0 -p 1 -g 3
./examples/programming_model/02_resource_lifecycle -d mlx5_0 -p 1
./examples/programming_model/03_rc_loopback -d mlx5_0 -p 1 -g 3
```

资源实验还提供这些参数：

| 参数 | 含义 | 默认值 |
|---|---|---|
| `-s` | MR 缓冲区字节数 | 4096 |
| `-c` | 请求的 CQ 容量 | 16 |
| `-w` | 请求的发送、接收 WR 容量 | 16 |
| `-e` | 每条发送、接收请求的 SGE 数量上限 | 1 |
| `-i` | 请求的 inline 字节上限 | 64 |

表 2-15：资源实验的可调参数。

请求容量可能受设备限制，实际获准值以返回结果为准。若设备不支持所请求的 inline 大小，可先用 `-i 0` 建立基线，再逐步试验。每次只改一个参数，比较请求值、返回能力和创建结果。

环境准备见[首次运行教程](../../docs/01-introduction/02-environment-and-first-program.md)，完整讲解见[编程模型](../../docs/02-programming-model/index.md)。清理本目录生成的二进制可执行：

```bash
make -C examples/programming_model clean
```
