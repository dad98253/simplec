
This program is a text based TCP/UDP server. It listens on a specified port
for remote debug client connections. When a client connects, the server processes
the ANSI (color) text data received and displays it on the console window. On
linux, it depends on the console to process the ANSI escape sequences. On Windows,
it converts the ANSI codes to the equivalent Windows console "set color" commands.

Usage:
	simples -p [protocol] -e [port] -i [ipaddr]

Where,
 protocol is either TCP or UDP,

 port is the port number to listen on,

 ipaddr is the IP address to bind to. Hosts with just one NIC can ignore this.
