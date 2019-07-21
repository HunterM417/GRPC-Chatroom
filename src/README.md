Class: CSCE 438
Section: 500

Coder: M. Hunter Martin
Partner: Trenton Smith

## Instructions:

Compile the code using the provided makefile:

    make

To clear the directory (and remove .txt files):
   
    make clean

To run the routing server on port 3010:

    ./startup.py -r -p 3010:

To run a master (and slave) server on port 3020, connecting to a routing server at address "10.0.2.15:3010":

    ./startup.py -m -p 3020 --host 10.0.2.15 --routing-port 3010

To run the client that will connect to a routing server at address "10.0.2.15:3010":

    ./tsc -h 10.0.2.15 -p 3010 -u user1

## Testing Tips

One easy way to monitor master and slave processes is to use the system monitor.

You can multi-select processes in the system monitor to kill more than process in one go.

## Shared folder Note:

To share user information among the various master servers, I used a VirtualBox shared folder.  
I'm not sure if this was an option available to us for this assignment, but the shared folder effectively
acts as a network drive for the application.

To setup the shared folder, here's a [helpful guide](http://www.giannistsakiris.com/2008/04/09/virtualbox-access-windows-host-shared-folders-from-ubuntu-guest/)

Once the folder is setup, simply place the application into the folder and run the app on all your different VMs.

