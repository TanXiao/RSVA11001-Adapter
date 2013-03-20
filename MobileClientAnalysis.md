Rosewill RSAV11001 Mobile Client Analysis
==========

In the box with the Rosewill RSVA11001 was a CD-Rom. The CD contained 
a number of folders and files. There are clients for the following platforms on it

1. Android
   * Castillo Player+
   * Castillo Player for Android 1.5-2.1
   * Castillo Player for Android 2.2-2.3
2. J2ME - A client named only by "viewer.jar"
3. Symbian - Castillo Player 5.1.713
4. Windows Mobile - Castiilo Player 5.1.2
5. ActiveX - Named only by "FuDvrOcx_Snp.exe" 
6. Microsoft Windows
   * H264HDPlayer_Install.exe
   * H264HDPlayer_Install.exe

Odds are if you see a H.264 security DVR that is below $250 for sale
somewhere, it is one these units. In particular, if it advertises 
compatability with most platforms it is most definitely related to this
device somehow. The 4 channel devices are probably just missing one of the 
analog to digital chips. I only have experience with the Rosewill RSVA11001.

Since there was no shrink-wrap type license with the software I took the 
liberty of decompiling the Android and J2ME clients.

J2ME - Streaming JPEGs
--------

The J2ME client uses an HTTP connection that returns a series
of JPEG images from the server. Each image is a new frame from the camera.
Obviously this protocol is incredibly ineffecient in terms of bandwidth
but is simple to implement on the sending and receiving end.

The request looks like this

In this example, the security DVR has an IP address of 192.168.0.1

The client requests the URL `http://192.168.0.1/snapshot.html?username&password&PTZCONTROL=CH1`
This url contains the username and password in the query string, which is poorly
formatted. The final part of the query string is the camera to select. On
the back of the RSVA11001 the video inputs are numbered 1-8 and directly
correlated with the `PTZCONTROL` parameter of the query string.

Interestingly the J2ME client sends an HTTP authorization header. The
server appears to ignore all headers and instead only pays any attention
to the `GET` line of the request line. An inspection of the server executable
indicates it is knowledgeable of the 'Authorization' HTTP header, but 
may not actually do anything with it on this URL.

After a delay, the server responds with a fixed length reply and begins
to send JPEGS. Each JPEG is encapsulated in the following binary format

<table>
<tr>
<td>"JPGS"</td>
<td>4 character ASCII string, not null terminated</td>
</tr>
<tr>
<td>4 byte big endian unsigned integer</td> 
<td>Lenth of JPEG image</td>
</tr>
<tr>
<td>JPEG Payload</td>
</tr>
<tr>
<td>"JPGE"</td>
<td>4 character ASCII string, not null terminated</td>
</tr>
</table>

This same sequence repeats until either the server reaches the number
of bytes advertised in the `Content-Length` header or the client
closes the TCP Socket.

That is everything there is to this protocol. I'll note that if you have
one connection open and then make another, the server hangs up
the first request. So there is no chance of getting all the cameras at once
throught this interface.

Of note is that the JPEG returned is always 640x480. For whatever reason it
is upscaled from the NTSC camera sources I was using. The box uses the 
Common Intermediate Format resolution 352x240 internally from what I can
tell.

Castillo Player - Common to Android, iOS etc.
---------

This player appears to use a proprietary application layer protocol built
on top of TCP to communicate with the DVR. The application layer protocol
is built on top of TCP. The server listens on port 15961. 

Each and every message in both directions is encapsulated in the following
format. 

<table>
<tr>
<td>4 byte big endian unsigned integer</td>
<td>(Length of payload + length of sequence number) in bytes</td>
</tr>

<tr>
<td>4 byte little endian unsigned integer</td>
<td>Sequence number</td>
</tr>

<tr>
<td>Payload</td>
</tr>
</table>

This message format is from here on out referred to as 
_L_ength-*S*equence messages or LS.

The client always starts at sequence number zero and increments.
The server starts at an arbitrary number and counts up from there. On
a long-lived enough stream roll over should occur, but this has not
yet been observed. The client always ignores the sequence number provided
by the server. Given that TCP is a reliable protocol, this number is 
completely meaningless.

Interestingly the length provided in the first four bytes always
includes the length of the sequence number. To ignore the sequence number,
the four bytes after the length are discarded and the length of the 
payload is decremented by 4.

The payload is any number of messages of the following format

<table>
<tr>
<td>2 byte little endian unsigned integer</td>
<td>Type of payload</td>
</tr>

<tr>
<td>2 byte little endian unsigned integer</td>
<td>Length of payload in bytes</td>
</tr>

<tr>
<td>Payload</td>
</tr>

This message format is from here on out referred to as *T*ype-*L*ength-
*V*alue or TLV for short.

The following TLV message types have been observed to be sent by the
server

<table>
<tr>
<td>Type #</td>
<td>Type Name</td>
<td>Length (bytes)</td>
<td>Description</td>
</tr>

<tr>
<td>42</td>
<td>Login Response</td>
<td>4</td>
<td>Response to login</td>
</tr>

<tr>
<td>65</td>
<td>Channel Data Response</td>
<td>4</td>
<td>Information about what video channel is being viewed on the DVR</td>
</tr>

<tr>
<td>70</td>
<td>DVS Information Request</td>
<td>72</td>
<td>Used to trigger DRM code in Client</td>
</tr>

<tr>
<td>97</td>
<td>Audio</td>
<td>Variable</td>
<td>Format Unknown</td>
</tr>

<tr>
<td>98</td>
<td>Audio</td>
<td>Variable</td>
<td>Format Unknown</td>
</tr>

<tr>
<td>99</td>
<td>Video Frame Information</td>
<td>12</td>
<td>Information about the video frames</td>
</tr>

<tr>
<td>100</td>
<td>Video I Frame</td>
<td>Variable</td>
<td></td>
</tr>

<tr>
<td>102</td>
<td>Video P Frame</td>
<td>Variable</td>
<td></td>
</tr>

<tr>
<td>200</td>
<td>Stream Format Information</td>
<td>40</td>
<td>Information about the stream format</td>
</tr>
</table>

The client has been observed to send the following messages

<table>

<tr>
<td>Type #</td>
<td>Type Name</td>
<td>Length (bytes)</td>
<td>Description</td>
</tr>

<tr>
<td>40</td>
<td>Version Info</td>
<td>4</td>
<td>Major and Minor Version numbers</td>
</tr>

<tr>
<td>41</td>
<td>Login Request</td>
<td>56</td>
<td>Username and password</td>
</tr>

<tr>
<td>51</td>
<td>PTZ Control Request</td>
<td>8</td>
<td>Request to control camera Pan, Tilt, & Zoom</td>
</tr>

<tr>
<td>64</td>
<td>Channel Request</td>
<td>8</td>
<td>Request to change the channel</td>
</tr>

</table>

The Worst DRM You've Ever Seen
------
When the server sends a TLV message of type #70 it sends along a bunch
of information about itself. The client then RC4 'encrypts' them, and
makes an HTTP request to a PHP page on a webserver. The webserver returns
a base64 encoded string of an RC4 bytestream that the client then decodes
and compares to a 'secret' value. If they do not match, the client does
will not stream data from the device. The string returned by the PHP
page is a constant value for constant input.

It appears someone wanted a way to reject DVR manufacturers use of the
viewer program after the fact. It won't work because the HTTP response
could easily be faked and secondarily the firmware could be updated
on the DVR to report as another server. Either way it is a massive
infringment on the privacy of the users.

Packet captures from Castillo Player 6, available from the Google
Play store, reveal this behavior has been removed most likely. It's probably
not a good idea to require your clients to always have an active internet
connection to use a very offline device.












