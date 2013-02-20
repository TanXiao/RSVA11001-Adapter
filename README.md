RSVA11001 Adapter
=================

The [RSVA11001](http://www.newegg.com/Product/Product.aspx?Item=N82E16881147011) is a home surveillance DVR Kit sold by Newegg. Rosewill
is the 'house brand' of Newegg. The DVR box is identical to the one
sold with the RSVA12001, the difference is supposedly in the cameras.
The DVR is most likely not made explicitly for Newegg. It is a repackaged 
[HiSilicon 3515](http://www.secuv.com/Hi3515-H.264-Encoding-and-Decoding-Processor)
or [HiSilicon 3520](http://www.secuv.com/Hi3520-H.264-Codec). The only difference
is the number of channels as far as I can tell. The Huawei 3515 is probably
the same product as well.

I purchased this kit because it was reasonably priced and seemed to 
be packed full of features. I assumed that since it advertised a web
interface I could just use some open source software like 
[motion](http://www.lavrsen.dk/foswiki/bin/view/Motion/WebHome) to scrape
the web interface and get most of what I needed from that. 

Initial Impressions
-------
The HTTP server on the box is extremely glitchy. While Newegg claims it is linux
powered, my guess is that all the code is actually running kernel mode
or something equally bizarre.
Sending it **well-formed** HTTP requests can crash it if the request
is not in the tiny grammar that the DVR accepts. It apprently does have
some sort of hardware-watchdog, because the box will thankfully reboot
itself about 20 seconds afterwards. Just browsing to a random URL
from my browser was enough to kill it in my tests.

Once I got it up and running I realized its ActiveX primarily,
which puts it firmly out of my reach since I am a linux user only. It 
appears to try and do some User-Agent detection. If you browse to it from
Firefox under Ubuntu it will give you a page that somehow gets the browser
to open the RTSP connection on the DVR(the actual images are not delivered
over HTTP).

I did some browsing on the included CD-ROM and concluded the following

* The CD includes almost no documentation, but a variety of clients.
* There are clients for J2ME, Android, iOS and other stuff I didn't 
recognize.
* The Android client is called 'Castillo Player'. Decompiliation with
jad showed me that it uses an apparently proprietary streaming tech. I 
assume the iOS version is the same thing in obj-c. Both versions had
some sort of blob that I think is native code for the target platforms.
* The J2ME version uses a web interface that does long-polling to 
return a MJPEG-ish stream. It returns up to some finite amount of 
bytes then closes the connection.
* All the coding was mostly likely done by people with little to no
best-practices experience, but a 'can do' attitude. It does work, but 
things like function calls are passed over in preference to complicated
while loops.

Problem
----
After some more testing I also figured out that no matter what you do,
no more than one client can be using any of the interfaces at once. A
second client connecting just boots the first or freezes it. This immediately
rules out most open-source motion detection software since it is has 
no concept of access exclusion. Plus, if you live in a shared housing 
arrangement it'd make sense that more than one person might want to check
up on the house at a time. 

Another note is that opening the RTSP stream takes eons. It 
seems to take  at least 30 seconds. Then no matter what it just cuts off after a
few minutes. So I consider it useless.

Solution
------
So, the most obvious solution I could think of is to write a proxy
that translates all requests to the box. I ended up writing some software
to just continously round-robin the JPEG stream from the web interface.
It writes it into files. They can be served with a web server to any
number of clients. Currently the cycle to round-robin is 

1. Open a connection
2. Send the HTTP request
3. Wait for at least one JPEG image
4. Write JPEG image to file
5. Close the connection.
6. Goto the next image stream.

The present resulting FPS is rather slow. Certainly not full motion
video like the cameras can deliver. But it does work!
I plan to improve things but presently I am just happy this works at all.
Right now it is just a proof-of-concept. I plan to finish converting
it into a useful piece of software soon.

One of the major issues I've noticed is that sometimes the box 
timestamps the images retrived from the web interface. But not all
 the time. 

Firmware
====

Not content with an external software solution, I've also began examining the
firmware to see if I can load my own stack on it.

Format
---
As deduced so far, not exact

    Magic 4 byte header - 73 56 EE 3A
    Some strings indicating the version of the firmware
    [uImage](http://git.denx.de/cgi-bin/gitweb.cgi?p=u-boot.git;a=blob;f=include/image.h) of a 'hilinux' kernel for ARM 
    [gzip](http://www.gzip.org/zlib/rfc-gzip.html#header-trailer) of an mystery blob, see blow
    [JFFS2](http://sourceware.org/jffs2/jffs2-html/node3.html) root filesystem
    [CRAMFS](http://en.wikipedia.org/wiki/Cramfs) filesystem of the applications
    
It seems that this device is implemented backwards in terms of the logical use of the filesystems.
It would be logical to have a CRAMFS root filesystem, since that should only be readonly. With JFFS2
mounted read-write for the application layer. However, they have done the opposite.

I'm not sure if any of this is positioned at absolute offsets in the file or not.

Investigation
---
If you goto the [product page](http://www.rosewill.com/support/Support_Download.aspx?ids=26_133_412_1952)
you can download what appears to be firmware for the device. There is no 
explanation of what it is, or how to upgrade the device. The manual just says
to put it on a USB stick and boot the unit. I'm guessing you'd need to extract it first
and format the USB with a FAT32 filesystem, but I have not tried.

Inspecting the firmware
with a hex editor reveals strings such as  "U-Boot 2008.10-svn (Dec  3 2012 - 15:25:46)".
This led me to conclude that the device is using GPL'd software.  Other symbols in the blob are

    nand_select_chip
    nand_transfer_oob
    nand_fill_oob
    nand_scan_tail

These are definitely linux kernel functions. I also found the following
linux kernel image boot lines

    console=ttyAMA0,115200 root=1f02 rootfstype=jffs2 mtdparts=physmap-flash.0:384K(boot),1280K(kernel),5M(rootfs),9M(app),128K(para),128K(init_logo) busclk=220000000
    bootargs=console=ttyAMA0,115200 root=1f02 rootfstype=jffs2 mtdparts=physmap-flash.0:384K(boot),1280K(kernel),5M(rootfs),9M(app),128K(para),128K(init_logo) busclk=220000000
    bootcmd=showlogo;
    bootm 0x80060000
    bootdelay=1
    baudrate=115200
    ethaddr=00:16:55:00:00:00
    ipaddr=192.168.0.100
    serverip=192.168.0.1
    gatewayip=192.168.0.1
    netmask=255.255.255.0
    bootfile="uImage"

Apparently ttyAMA0 refers to a serial port on the SoC. Inside the box
there is a 5 pin port labeled UART0. Based on these values, U Boot is configured to try and retrieve a file from a TFTP server and boot that.
Unforunately when it boots, I do not see any requests for 192.168.0.1 coming from the box
in a wireshark packet capture with my laptop. Perhaps it is the very
strange ethernet address setting. Also, I think the physmap of the flash is not actually
what is being loaded.  All that about a UART connection and TFTP looked promising but has not 
been exploited yet. Perhaps this is somehow an unused configuration.

These lines are the password for the root user in /etc/shadow format

    root:$1$$qRPK7m23GJusamGpoGLby/:0:0::/root:/bin/sh
    root:ab8nBoH3mb8.g:0:0::/root:/bin/sh
    
If someone wants to decrypt those, it would be of a great aid. A rainbow
table can probably be used.

After more browsing I realized I was just looking at a JFFS2 filesystem
the hard way, so I extracted it from the firmware.

Based on the 'bootargs' paramter above, I kept fumbling around thinking
that that I needed to find six total separate JFFS2 images. However,
[binwalk](http://code.google.com/p/binwalk/) helped me figure out what was
really going on. It has a CRAMFS and a JFFS2 filesystem. Binwalk gets some
false positives on some LZMA data in there but is otherwise a great help.
Both filesystems are extracted and are under this repo now.

I have located the [original kernel and patches](http://lwn.net/Articles/266705/). But
redhat no longer makes the patches available, but it looks like it is 
[available here](http://www.kernel.org/pub/linux/kernel/projects/rt/2.6.24/older/patch-2.6.24-rt1.bz2).
Either way, I put the vanilla kernel and the patch in this repo.

Interestingly, the files I have extracted include unstripped exectuable object files.
The device has far more flash memory than needed, the firmware is zero padded
to the required size. It appears that files that are stripped are the final
manufacturers exectuables (just branding really) and the unstripped files
are the SDK for the device provided by the OEM. So this is probably 
a the same exact release used for development.

I have located the SDK for the HiSilicon H3511, but it is linux
2.6.14 based so it is not identical. It would be fantastic to locate the
HI3515 or HI3520 SDK.


Mounting the JFFS2 image
---
Special care must be taken to properly mount the JFFS2 image. A
NAND device should be simulated for this purpose. This is an example 
of how I did on it Ubuntu linux.

   modprobe mtdblock
   modprobe mtdchar
   modprobe mtdram erase_size=128 total_size=32768
   flash_eraseall /dev/mtd0
   nandwrite /dev/mtd0 jffs2_img
   mount -t jffs2 /dev/mtdblock0 /mnt
   
Mystery Blob
---
The mystery blob from the firmware is a gzip compressed item. Once
decompressed, the format is not obvious. Binwalk finds at least two ELF32
objects in it. However, running 'strings' find goodies such as 

   Linux version 2.6.24-rt1-hi3515v100 (bruce@ubuntu) (gcc version 3.4.3 (release) (CodeSourcery ARM Q3cvs 2004)) #30 Thu Nov 22 19:29:06 CST 2012
   
CodeSourcery is a Mentor Graphics tool chain for packaging embedded linux kernels.
Based on all the other ASCII values in this blob, this appears to be 
a completely seperate copy of a compiled kernel. I have to wonder, was this
gzip'd blob included on accident?

Software Stack
---
The software stack is 

* U-Boot Universal Loader
* Linux 2.6.24-rt1
* A number of proprietary kernel modules for the special purpose hardware, for example one object file is named 74hc1605 which is a discrete flip-flop.
* Proprietary kernel modules for hardware encoding of video, etc. There are some files referencing an FPGA, so this is a very general purpose core.
* Busybox userspace
* More stuff I have not analyzed yet

GPL Compliance
---
It should be possible to force them to release the source code, or at least force
Newegg since it is an American company. It seems unlikely that [Huawei will cooperate](http://huaweihg612hacking.wordpress.com/2011/11/12/huawei-releases-source-code-for-hg612/).
I have contacted Rosewill but have not received a reply.
   

Hardware
========

* Single board construction
* FPGA-based 32-bit ARM SoC, little-endian
* mPower mP2809 - 4 channel video + 5 channel audio
* mPower mP2807 - 4 channel video
* Realtek RTL8201CP - 10/100 Mbps Ethernet
* 16 megabytes of non-volatile flash memory
* Hynix memory chips
* A single relay which seems to be related to the RS-432 port
* A 74HC140 (Hex-inverting schmitt trigger) that appears to be used for the ALARM port
* A port labelled UART0
* Two SATA ports
* Some miscellaneous power filtering and regulation analog components.

The mP2809 has a single DAC, I think that is how the device does 
composite out. It's possible its in the SoC. Composite is not a particularly
complex signal.

I have no idea what does the VGA. It must be in the SoC 
as well.

Interestingly, there does not appear to a dedicated SATA controller. It
is most likely integrated into the SoC.


Credits
----
The HTTP Parser included in the software is joyent/http-parser available
on GitHub. Many thanks
to presenting a simple interface and a library free from a million
dependencies. Also, many thanks to the whole CMake team for writing
a build manager that doesn't suck.
