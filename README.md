# preloader
[![License: MIT](https://img.shields.io/badge/License-MIT-blueviolet.svg)](https://opensource.org/licenses/MIT)
[![CI](https://github.com/Theldus/preloader/actions/workflows/c-cpp.yml/badge.svg)](https://github.com/Theldus/preloader/actions/workflows/c-cpp.yml)

Preloader 'pre-loads' dynamically linked executables to speed up their load
times

## Why?

While dynamically linked executables have several advantages over static binaries,
excessive use of dynamic libraries might lead to longer program load times,
whether due to extensive symbol lookup, number of relocations, binary load, and
etc.

Usually this is not noticeable, but it becomes visible in short-lived processes,
especially on 'older' hardware (e.g., Pentium Dual Core) and embedded devices
like the Raspberry Pi. In this scenario, the load time can consume a significant
amount of the execution time, or even more than the program's runtime.

## How it works?

TL;DR: Preloader functions by injecting a library into the binary to be preloaded.
Following injection, a server is created that listens for fork() requests,
preventing libraries from being loaded again.

<details><summary>Longer explanation</summary>

Preloader is divided into two parts: client and server. The image below
depicts the server's operation (in a simplified manner), but an explanation of
both follows below:

![preloader](https://user-images.githubusercontent.com/8294550/192086677-2261405a-4cfd-4ec7-a58c-fbe75f486cb6.png)

Preloader works by injecting a dynamic library (libpreloader.so) to be preloaded
while executing the desired binary. When injected (the library), the library's
'constructor' modifies the entry point of 'foo' so that when 'foo' is executed,
the preloader's main routine (`preloader_main`) is executed instead of '`_start`'.

Once '`preloader_main`' is executed, the dynamic loader has already loaded all
dynamic libraries from that binary, as it was supposed to be for `_start/main`
to be running.

Following that, preloader launches a server that listens for connections from
'_preloader_cli_.' The parameters used in the process execution are sent to each
established connection. After receiving the parameters, a fork is performed,
and the new child process configures the final steps (such as changing the
argv) to begin. As a final step, the initial entry point is restored, and the
child process resumes normal execution.

</details>

## Usage

Preloader consists of two main components: `preloader` and `preloader_cli`. The
first (preloader) is in charge of starting a server and preloading the program.
The second (preloader_cli) is the client that connects to the server and requests
that a new process is launched with the parameters provided.

The preloader_cli is meant to work transparently to the user, as if it were the
process itself, by forwarding (and receiving) standard input and outputs, signals,
return code, and such to the original process. The idea is that it acts as a
'drop-in replacement' for the actual command, behaving similarly.

Below are some examples of use cases and how to apply the preloader features to
them:

### Basic usage

The most basic example is to run a server, and in another terminal, run the
client:
```bash
# Terminal 1: start the server
$ preloader /usr/bin/foo
# or
$ preloader foo # (if 'foo' is in the PATH)

# Terminal 2: launch a preloaded process with the parameters: a, b and c:
$ preloader_cli foo a b c
```

### Daemon mode `-d,--daemonize`:
<details><summary>Click to expand</summary>

The recommended way to run the preloader is however in daemon mode. In daemon
mode, preloader runs as a standalone process, just like any server normally
does:
```bash
$ preloader -d foo
$ preloader_cli foo a b c

# If want to stop the daemon:
$ preloader -s # (or --stop)
```
</details>

### Bind mode `-b,--bind-now`:
<details><summary>Click to expand</summary>

Preloader also supports immediate binding of the process to be executed
(LD_BIND_NOW/RTLD_NOW). As a result, all symbols are resolved at startup rather
than later (lazy binding).

Generally immediate binding increases the program load time, but for the
preloader it is very beneficial since this loading occurs only once:

```bash
# Launches a daemon with bind-now of foo:
$ preloader -b -d foo
$ preloader_cli foo param1 param2...
$ preloader -s
```

This option is not enabled by default, but its use is highly recommended.
</details>

### Preload dlopen'ed libs with `-f,--load-libs` / `getlibs.sh`:
<details><summary>Click to expand</summary>

Sometimes there is a need to preload libraries loaded at runtime (i.e., via
dlopen()). This can happen in different scenarios, such as loading plugins and
libraries for a program or some programming language like Python. For this,
preloader allows preloading these libraries by providing a txt file containing
one library path per line.

To make this task easier, preloader includes the `utils/getlibs.sh` tool, which
is a bash script that executes the command that the user wants to preload and
generates a txt file containing all of the libraries loaded by that program
until it terminates.

The general procedure is as follows:
```bash
#
# Launch the process normally by using getlibs.sh and extract all
# libraries used into the foolibs.txt file
#
$ ./utils/getlibs.sh -o foolibs.txt myfoo param1 param2 ... paramN

# Launch preloader and preload the foolibs.txt file
$ preloader -f foolibs.txt myfoo

# Launch client normally
$ preloader_cli myfoo param1 param2 ... paramN
```
</details>

### Preloading multiple processes
<details><summary>Click to expand</summary>

Preloader also allows multiple instances to run simultaneously, as long as they
each use a different 'port'¹ number. To specify a different port, use the
`-p,--port` flag.

If you want to preload foo and bar at the same time, you could do:
```bash
$ preloader -d -p 4040 foo
$ preloader -d -p 4041 bar

# Later
$ preloader_cli -p 4040 foo arg1 argN
$ preloader_cli -p 4041 bar a b c

# Stopping them
$ preloader -s -p 4040
$ preloader -s -p 4041
```

¹Note: Please note that 'port' does not imply a TCP/UDP port. Preloader uses Unix
Domain Socket for IPC and the port number only serves to compose the socket file
name and distinguish between multiple instances.
</details>

### Transparent preloading
<details><summary>Click to expand</summary>

The '`preloader_cli`' tool works similarly to BusyBox: the argv[0] of preloader_cli
determines the argv[0] of the new process. If preloader_cli's first argument is
not its own name (as in a symbolic link or if the file is renamed), preloader_cli
will use this as the program's first argument.

If you want to preload `clang` completely transparently to some program, you
could do the following:

```bash
cd /home/user/preloader
ln -s preloader_cli clang
export PATH=$PWD:$PATH
```

In this case, the symlink takes precedence in the PATH and is invoked instead
of the original program.
</details>

### Tools: `ltime` and `finder`:
<details><summary>Click to expand</summary>

Preloader also includes tools for evaluating potential programs that can benefit
from preloading, such as `finder` and `ltime`.

#### Finder
Finder recursively analyzes one or more paths and displays the total number of
relocations (of the executable and all dependent libraries) and the total number
of dynamic libraries required.

Obtaining the number of relocations *and* libraries gives an idea of how big an
executable can be. After all, an executable with two libraries and 500k relocs
can be as large as one with one hundred libraries and 500k relocs.

Here are the top-5 programs with the most relocations on my system (Slackware
14.2-current, 15-ish):

```text
/usr/lib64/qt5/libexec/QtWebEngineProcess  196   615522
/usr/bin/lldb-vscode                        99   480372
/usr/bin/lldb                               99   480166
/usr/bin/clang-11                           95   403394
/usr/bin/c-index-test                       93   399004
```
and the top-5 ordered by library amount:
```text
/usr/lib64/qt5/libexec/QtWebEngineProcess   196  615522
/usr/bin/SDLvncviewer                       190  171352
/usr/bin/ffprobe                            187  198403
/usr/bin/ffplay                             187  198472
/usr/bin/ffmpeg                             187  198544
```

#### Ltime

Ltime can also take multiple paths and parameters and analyzes them recursively,
just like the finder. However, the purpose of ltime is to analyze program load
time with and without preloader.

The loading time considered by ltime is the time it takes a program to reach its
entry point (usually `_start`) since, at that point, all libraries have already
been loaded by the dynamic loader. Please note that the load times do not reflect
libraries opened later at runtime. This is the case for many programs, such as
firefox.

Below are the top-5 load times with and without preloader (i5 7300HQ):
```text
"/usr/bin/mplayer",                          52.136383 ms, 1.689277 ms
"/usr/bin/ffprobe",                          49.085384 ms, 1.490358 ms
"/usr/lib64/qt5/libexec/QtWebEngineProcess", 45.864020 ms, 1.565409 ms
"/usr/bin/SDLvncviewer",                     41.423378 ms, 1.487050 ms
"/usr/bin/ffmpeg",                           37.896008 ms, 1.410382 ms
```

In [this](https://gist.github.com/Theldus/dc08a7183be4d6852d49e8b322bedfd9)
link you can find the runtimes with and without preloader of all
4645 executables on my system (Slackware 14.2-current + i5 7300HQ).

A detailed description about these tools can be found in their respective
source code: [ltime.c](utils/ltime.c), [finder.c](utils/finder.c)
</details>

---

More information about the features can be found with `-h, --help` and in the
[preloader.1](doc/man1/preloader.1) and [preloader_cli.1](doc/man1/preloader_cli.1)
manpages.

## Benchmarks

Two programs (`clang` and `ffmpeg`) were tested in four execution environments
to estimate the preloader's performance. The number of shared libraries, total
number of relocations (program + libraries), and load times with and without
preloader were obtained for each execution environment:

**Environment 1**: Intel Core i5 7300HQ (AMD64) + Slackware 14.2-current
(15-ish), kernel v5.4.186

|     Program    | No. of shared libs | No. of relocations | AVG load time (without preloader)| AVG load time (with preloader)|
|:--------------:|:------------------:|:------------------:|:--------------------------------:|:-----------------------------:|
|  clang v11.0.0 |         95         |       403394       |        21.4 ms (± 0.21 ms)       |      1.29 ms (± 0.03 ms)      |
| ffprobe v4.3.1 |         187        |       198403       |       38.86 ms (± 0.72 ms)       |      1.76 ms (± 0.05 ms)      |


**Environment 2**: Intel Pentium T3200 (AMD64) + Slackware 15, kernel v5.15.19

|     Program    | No. of shared libs | No. of relocations | AVG load time (without preloader)| AVG load time (with preloader)|
|:--------------:|:------------------:|:------------------:|:--------------------------------:|:-----------------------------:|
|  clang v13.0.0 |         95         |       524043       |        70.9 ms (± 0.21 ms)       |      3.94 ms (± 0.01 ms)      |
| ffprobe v4.4.1 |         204        |       258641       |      153.36 ms (± 2.97 ms)       |      5.63 ms (± 0.04 ms)      |

**Environment 3**: Snapdragon 636 (AArch64) + Android 10 (with Termux), kernel
v4.4.192-perf+

|     Program    | No. of shared libs | No. of relocations | AVG load time (without preloader)| AVG load time (with preloader)|
|:--------------:|:------------------:|:------------------:|:--------------------------------:|:-----------------------------:|
|  clang v14.0.6 |         12         |       516887       |       122.43 ms (± 8.49 ms)      |       30 ms (± 7.45 ms)       |
| ffprobe v5.0.1 |         57         |        88878       |       90.39 ms (± 7.73 ms)       |      28.92 ms (± 7.96 ms)     |

**Environment 4**: Raspberry Pi 1B rev 2 (armv6) + Raspbian 10, kernel v5.4.51+

|     Program    | No. of shared libs | No. of relocations | AVG load time (without preloader)| AVG load time (with preloader)|
|:--------------:|:------------------:|:------------------:|:--------------------------------:|:-----------------------------:|
|  clang v7.0.1  |         15         |       191459       |       219.9 ms (± 0.92 ms)       |      27.87 ms (± 0.18 ms)     |
| ffprobe v4.1.9 |         187        |       341899       |       2698.41 ms (± 22 ms)       |      47.12 ms (± 0.63 ms)     |

The amount of shared libs and relocations were determined using the 'finder'
tool, and the load times were determined using the 'ltime' tool; more info
on these tools can be found in the 'Usage' section.

The following are the reasons for using `clang` and `ffprobe`:
- Have a great amount of dependencies and relocations.
- Due to the lack of a GUI, time measurement is simple.
- Are short-lived, and load time can contribute a significant portion of total
runtime.
- Are 'scriptable', meaning they can be called dozens or hundreds of times.

However, there are many other programs that can benefit from the preloader.
Also, because clang is part of the LLVM project, it implies that many other
LLVM-based tools can benefit from it as well. The same goes for ffprobe and
any other program that makes use of the
FFmpeg libraries.

The table below shows various real-world scenarios in the four environments
described earlier:

| Description                                              |           Env 1 (w & w/o preloader)        |          Env 2 (w & w/o preloader)       |          Env 3 (w & w/o preloader)        |          Env 4 (w & w/o preloader)        |
|----------------------------------------------------------|:------------------------------------------:|:----------------------------------------:|:-----------------------------------------:|:-----------------------------------------:|
| clang --version                                          |   4.6 ms (± 0.4 ms) / 25.1 ms (± 0.9 ms)   |  13.1 ms (± 0.5 ms) / 75.5 ms (± 0.4 ms) |  60.9 ms (± 3.1 ms) / 152.1 ms (± 5.9 ms) |  118.6 ms (± 0.4 ms) / 314.ms (± 2.9 ms)  |
| ffprobe -version                                         |   3.8 ms (± 0.7 ms) / 40.4 ms (± 1.1 ms)   | 14.3 ms (± 0.7 ms) / 157.2 ms (± 2.7 ms) |  29.6 ms (± 2.8 ms) / 99.1 ms (± 0.8 ms)  |   116 ms (± 0.6 ms) / 2831 ms (± 11 ms)   |
| <sup>1</sup>ffprobe somefile.mp4                         | 15.2 ms (± 0.7 ms) / 51.3 ms (± 0.7 ms)    | 41.8 ms (± 0.2 ms) / 183.3 ms (± 1.7 ms) | 92.7 ms (± 4.7 ms) / 149.9 ms (± 5.8 ms)  | 705.7 ms (± 1.2 ms) / 3405 ms (± 16 ms)   |
| <sup>2</sup>clang -c empty_main.c                        |   8.3 ms (± 0.5 ms) / 30.9 ms (± 0.9 ms)   |  23.1 ms (± 0.6 ms) / 89.4 ms (± 0.3 ms) |  94.5 ms (± 6.9 ms) / 166.1 ms (± 7.4 ms) | 503.3 ms (± 0.6 ms) / 703.0 ms (± 5.1 ms) |
| <sup>3</sup>clang -c 1kloc.c                             |   25.8 ms (± 0.5 ms) / 49.9 ms (± 0.8 ms)  | 88.3 ms (± 0.4 ms) / 163.6 ms (± 0.4 ms) | 184.5 ms (± 7.4 ms) / 239.0 ms (± 3.1 ms) |    1366 ms (± 6 ms) / 1574 ms (± 4 ms)    |
| <sup>4</sup>Git #30cc8d w/ clang & -O0 (~494 obj files)  | 30.730 s (± 0.262 s) / 34.693 s (± 0.066s) |     4m38s (± 0.14s) / 5m10s (± 0.42s)    |              -- untested --<sup>5</sup>   |              -- untested --<sup>5</sup>   |
| <sup>4</sup>Qemu v4.4.1 w/ clang & -O0 (~6590 obj files) |      6m54s (± 0.16s) / 7m45s (± 0.23s)     |               -- untested --<sup>5</sup> |              -- untested --<sup>5</sup>   |              -- untested --<sup>5</sup>   |

### Notes

1. 'somefile.mp4' is literally any file. I've noticed that 'ffprobe \<file\>'
has roughly the same runtime for any file.
2. 'empty_main.c' is equivalent to: `int main(void){}`.
3. '1kloc.c' is equivalent to
[this](https://gist.github.com/Theldus/09ed2205aa5ba15cdf4571b71cd1c8fc#file-test_aqua-c)
file.
4. Builds that aren't optimized for Git and Qemu (using all cores available in
each environment). This is because the builds here are focused on rapid
debug-edit-build cycles, and thus, optimized builds (which are slower) are
not the priority. However, there is a time gain for builds with -O2 and -O3,
though it is not as significant as with -O0.
5. Scenarios marked 'untested' were not tested due to the long runtime.
6. It should be noted that the builds for Git and Qemu the time was measured for
the 'make' command, and thus, the total build time, not the cumulative total
of clang invocations (think that make can invoke other scripts and programs as
well).
7. Each test had at least one warmup run and three consecutive runs. The times
were obtained via [hyperfine](https://github.com/sharkdp/hyperfine).

## Related works

Optimizing load times for dynamically linked executables is not new, and
many works have been done on the subject, with
[prelink](https://people.redhat.com/jakub/prelink.pdf)
being the most prominent of them. Prelink achieves this by changing the ELF
structure of the executable (and all shared libs it depends on) and setting a
non-overlapping base address for them (that will serve as a hint to the dynamic
linker later). Of course, this is a *very* simple explanation. Prelink, in fact,
does complex things inside the ELF binary (while being fully reversible) to
achieve prelinking.

Unfortunately, prelink is no longer maintained (2004-2013) and no longer works
on recent Linux distributions. Even on older Linux distros, I haven't managed
to get it working properly, so I can't compare (performance-wise) with preloader.

In [Dynamic-prelink](http://worldcomp-proceedings.com/proc/p2014/ESA2955.pdf),
the authors propose a new approach in which libraries are not modified while
still benefiting from ASLR. The relocation information is saved in an external
cache file, and a new dynamic linker capable of prelinking (through its cache
file) is implemented. The results demonstrated that it was faster than
traditional ASLR (no prelink) but slower than Prelink. Unfortunately, no
performance comparisons are possible because the paper does not provide the
source code.

The authors of
[Performance Characterization of Prelinking and Preloading for Embedded Systems](http://www.cecs.uci.edu/~papers/esweek07/emsoft/p213.pdf)
propose a preloader-like approach: `fork+dlopen()`. The executable that needs to
be preloaded is rebuilt as a shared object so it can be loaded via dlopen(), and
its functions (such as 'main') invoked later. Unfortunately, no source code is
available for possible comparisons.

Preloader differs from them in: being much simpler and not requiring any changes
to the system or files. In addition, it achieves a great performance reduction
since the forks are from the target process itself, so all relocations
(in theory) are already resolved.

## Limitations

As the preloader is quite 'low-level', there are a number of limitations on the
environment it supports:

- Operating System: Linux-only
- Architectures supported: ARM32, ARM64, i386 and x86-64
- Libraries supported: GNU libc, Bionic, and uClibc-ng (do not work on Musl)
- System tools: Bash, grep, cut, any version
- GNU Make

## Should I use it?

It depends. A dynamic executable (with or without a preloader) will always be
slower than a static one, so the reasons to use it or not depend solely on the
reasons to use a static binary or not, which may include:

- **Availability**: There may not exist static packages for your desired program
(and build times can be very time consuming!!)
- **Internet issues**: Downloading a static package might take a long time.
Preloader is lightweight and should download quickly even on slower connections.
- **Disk space**: There may not be enough disk space to download a static version
of a program.

## Building/Installing

Preloader only requires a C99-compatible compiler and a Linux environment:

```bash
$ git clone https://github.com/Theldus/preloader.git
$ cd preloader/
$ make

# Optionally, if you want to install
$ make install # (PREFIX and DESTDIR allowed here)

# Building ltime and finder (requires libelf):
$ make finder
$ make ltime
```

## Contributing

Preloader is always open to the community and willing to accept contributions,
whether with issues, documentation, testing, new features, bugfixes, typos, and
etc. Welcome aboard.

## License and Authors

Preloader is licensed under MIT License. Written by Davidson Francis and
(hopefully) other
[contributors](https://github.com/Theldus/preloader/graphs/contributors).
