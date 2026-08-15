# Programming Model Examples

These examples support the second part of RDMA101. They are intentionally small and observable: each program prints the object or semantic point it is meant to show.

Build:

```bash
make -C examples/programming_model
```

Run the examples in order:

```bash
./examples/programming_model/01_device_info
./examples/programming_model/02_resource_lifecycle
./examples/programming_model/03_rc_loopback
```

Common options:

```bash
-d <device>     RDMA device name, such as mlx5_0 or rxe0
-p <port>       RDMA port number, default 1
-g <gid-index>  GID index, default 0
```

`01_device_info` only inspects devices. `02_resource_lifecycle` creates and destroys Context-dependent resources. `03_rc_loopback` creates two RC QPs in one process and runs SEND, RDMA WRITE, RDMA READ, and Atomic CAS when the device reports atomic support.
