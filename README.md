
# TCP & Serial loopback tester

This loopback tester was made to test and debug esp8266-nonos-sdk's ip stack.

It is aimed to:
* resend same data on TCP socket
* resend same data on serial port
* send to and check data back from TCP socket
* send to and check data back from serial port

## basic local example:

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
## examples with esp8266/Arduino

The https://github.com/d-a-v/uStream arduino library with its examples is needed.

### serial loopback

On esp8266, flash the sketch ```EchoSerial.ino```.

The serial checker can be used this way, with the initial flush option:

```
$ ./tcpechotester  -R -y /dev/ttyUSB0 -b 115200 -m 8n1 -f
```

### TCP loopback

On esp8266, flash the sketch ```EchoTCP6969.ino```.

The TCP checker can be used this way (default port 6969):

```
$ ./tcpechotester  -C -d 1.2.3.4
```

### serial <-> TCP passthrough

On esp8266, flash the sketch ```TcpSerial.ino```.

This data flow can be tested:

```
PC(checker) =wlan=> esp8266 =serial=> PC(repeater) =serial=> esp8266 =wlan=> PC(checker)
```

On one console, start the serial repeater for the serial port:
```
$ ./tcpechotester  -R -y /dev/ttyUSB0 -b 115200 -m 8n1
```
Then, on another console, start the TCP checker, with the initial flush option:
```
$ ./tcpechotester  -C -d 1.2.3.4 -p 23 -f
```

## cmdline Help:

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

TCP client:
-d host set tcp remote host name
-p n    set tcp port (default 6969)
(otherwise act as TCP server)
```
