# i2c-pseudo driver

While I2C adapters are usually implemented in a kernel driver, they can also be
implemented in userspace through the `/dev/i2c-pseudo` interface. Load module
`i2c-pseudo` for this.

Use cases for userspace I2C adapters include:

* Using standard Linux I2C interfaces, particularly `/dev/i2c-%d`, to control
  I2C on remote systems under test managed by userspace software.

* Mocking the behavior of I2C devices for regression testing Linux drivers and
  software that use I2C.

## Details

Open `/dev/i2c-pseudo` and call ioctl `I2CP_IOCTL_START` to create a new
I2C adapter on the system. The adapter will live until its file descriptor is
closed. Multiple pseudo adapters can co-exist simultaneously, controlled by the
same or different userspace processes.

When an I2C device driver sends an I2C transaction to a pseudo adapter, the
transaction becomes available via ioctl `I2CP_IOCTL_XFER_REQ`. If a reply is
issued via ioctl `I2CP_IOCTL_XFER_REPLY` before the adapter timeout expires,
that reply will be sent back to the I2C device driver.

By default `I2CP_IOCTL_XFER_REQ` will block indefinitely for an
I2C transaction request to come in from an I2C device driver. Use `O_NONBLOCK`
for non-blocking behavior. Other ioctl commands are unaffected by `O_NONBLOCK`
and never block indefinitely.

Polling is supported, in blocking or non-blocking mode. `EPOLLIN` indicates an
I2C transaction is available for `I2CP_IOCTL_XFER_REQ`. `EPOLLOUT` indicates
`I2CP_IOCTL_XFER_REPLY` will not block, which is always the case and so is
never worth polling for. `EPOLLHUP` indicates `I2CP_IOCTL_SHUTDOWN` has been
called.

While polling is fully functional in blocking mode, it cannot be used to avoid
blocking. If a pending I2C transaction request times out between receiving
`EPOLLIN` and issuing `I2CP_IOCTL_XFER_REQ` the latter can block.
`O_NONBLOCK` is needed to avoid this.

The ioctl `I2CP_IOCTL_SHUTDOWN` will unblock any pollers or blocked
`I2CP_IOCTL_XFER_REQ`, as a convenience for a multi-threaded or multi-process
program that wants to exit. Use of `I2CP_IOCTL_SHUTDOWN` is purely optional.

See `include/uapi/linux/i2c-pseudo.h` for a detailed description of ioctl
args, usage, and behavior.

## Example userspace I2C adapter

At the bottom of this file is a simple program that starts one I2C adapter and
prints the I2C transfers it receives, with reads filled from stdin.

Sample usage if the code were placed into `example-i2c-adapter.c`:

```
$ sudo modprobe i2c-pseudo
$ cc -o example-i2c-adapter example-i2c-adapter.c
$ ./example-i2c-adapter < /dev/urandom
adapter_num=12
```

Use a different terminal to issue I2C transfers to its I2C adapter number:

```
$ sudo modprobe i2c-dev
$ i2ctransfer -y 12 w2@0x20 0x03 0x5A w3@0x77 0x2A+
$ i2ctransfer -y 12 w2@0x20 0x03 0x5A r5@0x75
$ i2ctransfer -y 12 w5@0x70 0xC2 0xFF=
$ i2ctransfer -y 12 w3@0x20 0x1A+ r2
```

With the data read from `/dev/urandom` the full exchange might look like this
on the `i2c-transfer` side:

```
$ i2ctransfer -y 12 w2@0x20 0x03 0x5A w3@0x77 0x2A+
$ i2ctransfer -y 12 w2@0x20 0x03 0x5A r5@0x75
0xd1 0x4f 0xc7 0xd7 0xa9
$ i2ctransfer -y 12 w5@0x70 0xC2 0xFF=
$ i2ctransfer -y 12 w3@0x20 0x1A+ r2
0x15 0xf4
```

And like this on the `example-i2c-adapter` side:

```
$ ./example-i2c-adapter < /dev/urandom
adapter_num=12

begin transaction
write addr=0x20 flags=0x200 len=2 buf=[0x03 0x5A]
write addr=0x77 flags=0x200 len=3 buf=[0x2A 0x2B 0x2C]
end transaction

begin transaction
write addr=0x20 flags=0x200 len=2 buf=[0x03 0x5A]
read addr=0x75 flags=0x201 len=5 buf=[0xD1 0x4F 0xC7 0xD7 0xA9]
end transaction

begin transaction
write addr=0x70 flags=0x200 len=5 buf=[0xC2 0xFF 0xFF 0xFF 0xFF]
end transaction

begin transaction
write addr=0x20 flags=0x200 len=3 buf=[0x1A 0x1B 0x1C]
read addr=0x20 flags=0x201 len=2 buf=[0x15 0xF4]
end transaction
```

The `example-i2c-adapter.c` program source code:

```
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include <linux/i2c.h>
#include <linux/i2c-pseudo.h>

#define MAX_MSGS_PER_XFER 6
#define MAX_DATA_PER_XFER 30

/* Start the I2C adapter. Returns 0 if successful, errno otherwise. */
static int start_adapter(int fd)
{
        int ret;
        struct i2cp_ioctl_start_arg start_arg = {
                .functionality = I2C_FUNC_I2C,
                .timeout_ms = 5000,
                .name = "example userspace I2C adapter",
        };
        if (ioctl(fd, I2CP_IOCTL_START, &start_arg) < 0) {
                ret = errno;
                perror("I2CP_IOCTL_START failed");
                return ret;
        }
        printf("adapter_num=%llu\n",
               (unsigned long long)start_arg.output.adapter_num);
        return 0;
}

/* Fill msg->buf from stdin. Returns 0 if successful, errno otherwise. */
static int fill_read_buf(struct i2c_msg *msg)
{
        int ret;
        for (int i = 0; i < msg->len; i += ret) {
                ret = read(0, &msg->buf[i], msg->len - i);
                if (ret < 0) {
                        ret = errno;
                        perror("stdin read() failed");
                        return ret;
                }
        }
        return 0;
}

/* Print msg to stdout. Returns 0 if successful, errno otherwise. */
static int print_msg(struct i2c_msg *msg)
{
        int ret;
        printf(msg->flags & I2C_M_RD ? "read" : "write");
        printf(" addr=0x%02hX flags=0x%02hX len=%hu ",
               (short)msg->addr, (short)msg->flags, (short)msg->len);
        if (msg->flags & I2C_M_RD) {
                /* stdin source may need to see the "read ..." request */
                fflush(stdout);
                ret = fill_read_buf(msg);
                if (ret) return ret;
        }
        for (int i = 0; i < msg->len; i++) {
                printf(i ? " " : "buf=[");
                printf("0x%02hhX", msg->buf[i]);
        }
        printf("]\n");
        return 0;
}

/* Process I2C transfers. Returns errno upon any interruption or failure. */
static int xfer_loop(int fd)
{
        int ret;
        struct i2c_msg msgs[MAX_MSGS_PER_XFER];
        __u8 data_buf[MAX_DATA_PER_XFER];
        struct i2cp_ioctl_xfer_req_arg req_arg = {
                .msgs = &msgs[0],
                .msgs_len = MAX_MSGS_PER_XFER,
                .data_buf = &data_buf[0],
                .data_buf_len = MAX_DATA_PER_XFER,
        };
        struct i2cp_ioctl_xfer_reply_arg reply_arg = {
                .msgs = &msgs[0],
        };

        for (;;) {
                if (ioctl(fd, I2CP_IOCTL_XFER_REQ, &req_arg) < 0) {
                        ret = errno;
                        perror("I2CP_IOCTL_XFER_REQ failed");
                        switch (ret) {
                        case ENOBUFS:
                        case EMSGSIZE:
                                break;
                        default:
                                return ret;
                        }
                        reply_arg.num_msgs = 0;
                        reply_arg.error = ret;
                } else {
                        reply_arg.num_msgs = 0;
                        reply_arg.error = 0;
                        printf("\nbegin transaction\n");
                        for (int i = 0; i < req_arg.output.num_msgs; i++) {
                                ret = print_msg(&msgs[i]);
                                if (ret) {
                                        reply_arg.error = ret;
                                        break;
                                }
                                reply_arg.num_msgs++;
                        }
                        if (!reply_arg.error)
                                printf("end transaction\n");
                }
                reply_arg.xfer_id = req_arg.output.xfer_id;
                if (ioctl(fd, I2CP_IOCTL_XFER_REPLY, &reply_arg) < 0) {
                        ret = errno;
                        perror("I2CP_IOCTL_XFER_REPLY failed");
                        if (!reply_arg.error) return ret;
                }
                if (reply_arg.error) return reply_arg.error;
        }
}

int main(void)
{
        int fd = open("/dev/i2c-pseudo", O_RDWR);
        if (fd < 0) {
                perror("Failed to open() i2c-pseudo device file");
                return 1;
        }
        if (start_adapter(fd))
                return 2;
        if (xfer_loop(fd))
                return 3;
        if (close(fd) < 0) {
                perror("Failed to close() i2c-pseudo device file");
                return 4;
        }
        return 0;
}
```
