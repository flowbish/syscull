# `syscull` - simulated syscall slaughter

## About

`syscull` is a tool to introduce failure into syscalls.
It uses ptrace(2) to attach to a process and spoof the return values of various system calls.

This tools was create for UIUC's System Programming course (CS 241) to help students test their code for robustness.
Syscall failures under normal operating conditions are often difficult to emulate, especially `fork()` failures.
This tool aims to provide a simple way to simulate these failures and ensure that robust, failure-tolerant code is written.

### Current syscall behaviour:
 * `read()`: Fails with `EINTR` every fifth read.
 * `write()`: Fails with `EINTR` every write read.
 * `fork()`: Fails when the environment variable `__SYSCULL_FORK` is set.
 * ... more to come, of course.
 
## Use

`syscull` does not have any external dependencies, simply clone this repo and run `make`.

```
$ git clone https://github.com/flowbish/syscull.git
$ make
```

From there you can just run `syscull` with some program as an argument.

```
$ ./syscull ~/cs241/shell/shell
```
 
This tool is currently in a very early stage but it shows a good proof of concept!
