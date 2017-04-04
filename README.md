
# TCP & Serial loopback tester

## Help:

```
** TCP echo tester - options are:
-h      this help
-f      flush input before start
-R      responder (read and send back)
-C      comparator (send and check back)

Comparator specifics:
-c n    use this char instead of random data
-c -1   increasing data from 0
-s n    size (instead of infinite)
-s -n   random size in [1..n]
-w n    pause output to ensure sizesent-sizerecv < n

Serial:
-y tty  use tty device
-b baud for tty device
-m 8n1  for tty device

TCP:
-n      set TCP_NODELAY option
-p n    set tcp port (default 6969)

TCP client:
-d host set tcp remote host name
(otherwise act as TCP server)
```

## Example:

On one console:
```
$ ./tcpechotester -R
bind & listen done.
waiting on port 6969
remote client arrived.
write: Connection reset by peer
```

On the other:
```
$ ./tcpechotester -d localhost -C
remote host:    localhost
port:           6969
connected to localhost.
[avg:1.55418 Gibps][now:1.6037 Gibps][size:1.40977 GiB]-----^C
```
