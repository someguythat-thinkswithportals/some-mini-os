```
 ___  ___  _ __ ___   ___       _ __ ___ (_)_ __ (_)       ___  ___ 
/ __|/ _ \| '_ ` _ \ / _ \_____| '_ ` _ \| | '_ \| |_____ / _ \/ __|
\__ \ (_) | | | | | |  __/_____| | | | | | | | | | |_____| (_) \__ \
|___/\___/|_| |_| |_|\___|     |_| |_| |_|_|_| |_|_|      \___/|___/

Some small, open-source, Unix-like operating system
```
<p align="center">
 <img src="screenshots/some-mini-os.png">
</p>

**some-mini-os** is a unix-like operating system that was made from scratch. has a custom kernel, custom libc known as ```some-libc``` and a custom filesystem. it currently supports x86_64 but in the future there might be a 32-bit version of the operating system.

# getting started
alright, before you can try out the operating system on a virtual machine, you need to install the following software:

* GCC
* NASM
* QEMU
* make

if you have linux you can type this into the terminal:

```bash
# debian/ubuntu
sudo apt install make gcc nasm qemu-system-x86-64 qemu-system-i386

# arch linux
sudo pacman -S qemu-system-x86_64 make gcc nasm qemu-system-i386
```
if you use windows you can use the windows subsystem for linux (wsl) to compile the operating system

anyways, after you installed all of the essential software, its time to compile the OS and run it inside of QEMU

if you want to compile the x86_64 version of the operating system
```bash
git clone https://github.com/someguythat-thinkswithportals/some-mini-os.git
cd some-mini-os
make all
qemu-system-x86_64 some-mini-os-x86_64.img
```
alternatively you can do this to compile it and run it inside of QEMU

```bash
git clone https://github.com/someguythat-thinkswithportals/some-mini-os.git
cd some-mini-os
make run
```
if you want to compile the i386 version of some-mini-os

```bash
git clone https://github.com/someguythat-thinkswithportals/some-mini-os.git
cd some-mini-os
make i386
qemu-system-i386 some-mini-os-i386.img
```
or you can do this
```bash
git clone https://github.com/someguythat-thinkswithportals/some-mini-os.git
cd some-mini-os
make-i386
```

## available make flags
```bash
make all # builds everything
make run # builds everything and runs in qemu
make run-serial # same thing as make run but it has serial stuff
make kernel # builds the kernel
make i386 # builds a i386 version of some-mini-os
make run-i386 # same thing as make i386 but it runs in qemu
make run-serial-i386 # same thing as make run-serial but its for i386
make clean
```
# license
this project is licensed under the [GPL-2.0 license](LICENSE)

and that's about it for now...